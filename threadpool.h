#ifndef __THREADPOOL_H
#define __THREADPOOL_H

#include <vector>
#include <queue>
#include <memory>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <thread>
#include <future>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 100;
 
enum class PoolMode
{
	MODE_FIXED,	// 固定数量的线程
	MODE_CACHED // 线程数量可动态增长
};

//线程类
class Thread
{
public:
	//线程函数对象类型
	using ThreadFunc = std::function<void(int)>;

	Thread(ThreadFunc func)
		:func_(func)
		, threadId_(generateId_++)
	{}
	~Thread() = default;

	//启动线程
	void start()
	{
		std::thread t(func_, threadId_);
		t.detach();
	}

	// 获取线程id
	int getId() const
	{
		return threadId_;
	}
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;
};

int Thread::generateId_ = 0;

//线程池类
class ThreadPool
{
public:
	ThreadPool()
		:initThreadSize_(0)
		, taskSize_(0)
		, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
		, poolMode_(PoolMode::MODE_FIXED)
		, isPoolRunning_(false)
		, idleThreadSize_(0)
		, threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
		, curThreadSize_(0)
	{}
	~ThreadPool()
	{
		isPoolRunning_ = false;
		// 等待线程池里所有线程返回
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		notEmpty_.notify_all();	// 放在锁后，避免notEmpty_.wait()无人唤醒导致的死锁
		exitCond_.wait(lock, [&]() { return threads_.size() == 0; });
	}

	//设置线程池的工作模式
	void setMode(PoolMode mode)
	{
		if (checkRunningState())
			return;
		poolMode_ = mode;
	}

	//设置task任务队列上限阈值
	void setTaskQueMaxThreshHold(int threshhold);

	// 设置线程池cached模式下线程阈值
	void setThreadSizeThreshHold(int threshhold);
	 
	//给线程池提交任务 用户调用该接口，传入任务对象，消费任务
	//Result submitTask(std::shared_ptr<Task> sp);
	template<typename Func, typename... Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
	{
		//打包任务，放入任务队列
		using RType = decltype(func(args...));
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		std::future<RType> result = task->get_future();

		// 获取锁
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		// 等待任务队列有空余
		if (!notFull_.wait_for(lock, std::chrono::seconds(1)
			, [&]()->bool {return taskQue_.size() < taskQueMaxThreshHold_; }))
		{
			//表示notFull等待1s,条件依然没有满足
			std::cerr << "task queue is full, submit task fail." << std::endl;
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType {return RType(); });
			(*task)();
			return task->get_future();
		}

		// 如果有空余，把任务放入任务队列中
		//taskQue_.emplace(sp);
		taskQue_.emplace([task]() {(*task)(); });
		taskSize_++;

		// 新放了任务，队列不为空，在notEmpty_上进行通知
		notEmpty_.notify_all();

		// cached模式 根据任务数量和空闲线程的数量，判断是否需要创建新的线程出来 场景：小而快的任务
		if (poolMode_ == PoolMode::MODE_CACHED
			&& taskSize_ > idleThreadSize_
			&& curThreadSize_ < threadSizeThreshHold_)
		{
			std::cout << "create new thread..." << std::endl;
			// 创建新线程
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
			// 启动线程
			threads_[threadId]->start();
			// 修改线程个数相关的变量
			curThreadSize_++;
			idleThreadSize_++;
		}

		// 返回任务的Result对象
		return result;
	}
	
	//开启线程池
	void start(int initThreadSize = std::thread::hardware_concurrency())
	{
		//设置线程池的运行状态
		isPoolRunning_ = true;
		//记录线程初始个数
		initThreadSize_ = initThreadSize;
		curThreadSize_ = initThreadSize;
		//创建线程对象
		for (int i = 0; i < initThreadSize_; i++)
		{
			// 创建thread线程对象的时候，把线程函数给thread线程对象
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
		}
		//启动所有线程
		for (int i = 0; i < initThreadSize_; i++)
		{
			threads_[i]->start();
			idleThreadSize_++;
		}
	}

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	//线程函数 线程池的所有线程从任务队列里面消费任务
	void threadFunc(int threadid)
	{
		auto lastTime = std::chrono::high_resolution_clock().now();

		for (;;)
		{
			Task task;
			{
				// 获取锁
				std::unique_lock<std::mutex> lock(taskQueMtx_);

				std::cout << "tid" << std::this_thread::get_id() << "尝试获取任务..." << std::endl;

				// cached模式下，有可能已经创建了很多的线程，但是空闲时间超过60s， 应该把多余的线程回收
				// 超过initThreadSize_数量的线程要进行回收
				// 当前时间 - 上一次线程执行的时间 > 60s 
				// 每一秒中返回一次 
				// 锁+双重判断
				while (taskQue_.size() == 0)
				{
					if (!isPoolRunning_)
					{
						// 线程池结束
						threads_.erase(threadid);
						exitCond_.notify_all();
						std::cout << "threadid:" << std::this_thread::get_id() << "exit" << std::endl;
						return;
					}

					if (poolMode_ == PoolMode::MODE_CACHED)
					{
						// 条件变量超时返回
						if (std::cv_status::timeout
							== notEmpty_.wait_for(lock, std::chrono::seconds(1)))
						{
							auto now = std::chrono::high_resolution_clock().now();
							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
							if (dur.count() >= THREAD_MAX_IDLE_TIME
								&& curThreadSize_ > initThreadSize_)
							{
								// 回收当前线程
								// 记录线程数量的相关变量的值修改
								// 把线程对象从线程列表容器中删除
								// threadid -> 线程对象 -> 删除
								threads_.erase(threadid);
								curThreadSize_--;
								idleThreadSize_--;

								std::cout << "threadid:" << std::this_thread::get_id() << "exit" << std::endl;
								return;
							}
						}
					}
					else
					{
						// 等待notEmpty条件
						notEmpty_.wait(lock);
					}

				}
				idleThreadSize_--;

				std::cout << "tid" << std::this_thread::get_id() << "获取任务成功..." << std::endl;

				// 从任务队列取一个任务出来
				task = taskQue_.front();
				taskQue_.pop();
				taskSize_--;

				// 如果依旧有剩余任务， 继续通知其他线程执行任务
				if (taskQue_.size() > 0)
				{
					notEmpty_.notify_all();
				}

				// 取出一个任务，通知可以继续提交生产任务
				notFull_.notify_all();
			}// 出作用域释放锁

			// 当前线程负责执行这个任务
			if (task != nullptr)
			{
				task();
			}
			idleThreadSize_++;
			lastTime = std::chrono::high_resolution_clock().now();	// 更新线程执行完任务的时间
		}
	}

	bool checkRunningState() const
	{
		return isPoolRunning_;
	}
private:
	//std::vector<std::unique_ptr<Thread>> threads_;	//线程列表
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;	 //线程列表
	int initThreadSize_;	//初始的线程数量
	int threadSizeThreshHold_;	//线程数量上限阈值
	std::atomic_int curThreadSize_;	// 当前线程池里线程的总数量
	std::atomic_int idleThreadSize_;	// 记录空闲线程的数量
	
	using Task = std::function<void()>;
	std::queue<Task> taskQue_;	//任务队列
	std::atomic_int taskSize_;	//任务数量
	int taskQueMaxThreshHold_;	//任务队列数量上限阈值

	std::mutex taskQueMtx_;		//保证任务队列的线程安全
	std::condition_variable notFull_;	//表示任务队列不满
	std::condition_variable notEmpty_;	//表示任务队列不空
	std::condition_variable exitCond_;	//等待线程资源全部回收

	PoolMode poolMode_;	//当前线程池的工作模式
	std::atomic_bool isPoolRunning_ = true;	//标识线程池是否启动
};

#endif // !__THREADPOOL_H
