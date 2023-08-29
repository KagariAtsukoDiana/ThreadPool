#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 100;

ThreadPool::ThreadPool()
	:initThreadSize_(0)
	,taskSize_(0)
	,taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	,poolMode_(PoolMode::MODE_FIXED)
	,isPoolRunning_(false)
	,idleThreadSize_(0)
	,threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
	,curThreadSize_(0)
{
}

ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;
	// 等待线程池里所有线程返回
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();	// 放在锁后，避免notEmpty_.wait()无人唤醒导致的死锁
	exitCond_.wait(lock, [&]() { return threads_.size() == 0; });
}

void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunningState())
		return;
	poolMode_ = mode;
}

void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
	if (checkRunningState())
		return; 
	if (poolMode_ == PoolMode::MODE_CACHED)
		taskQueMaxThreshHold_ = threshhold;
}

void ThreadPool::setThreadSizeThreshHold(int threshhold)
{
	if (checkRunningState())
		return;
	threadSizeThreshHold_ = threshhold;
}

Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	// 获取锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	// 等待任务队列有空余
	if (!notFull_.wait_for(lock, std::chrono::seconds(1)
		, [&]()->bool {return taskQue_.size() < taskQueMaxThreshHold_; }))
	{
		//表示notFull等待1s,条件依然没有满足
		std::cerr << "task queue is full, submit task fail." << std::endl;
		return Result(sp, false);
	}
	
	// 如果有空余，把任务放入任务队列中
	taskQue_.emplace(sp);
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
	return Result(sp);
}

void ThreadPool::start(int initThreadSize)
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

void ThreadPool::threadFunc(int threadid)
{
	auto lastTime = std::chrono::high_resolution_clock().now();

	while (isPoolRunning_)
	{
		std::shared_ptr<Task> task;

		{
			// 获取锁
			std::unique_lock<std::mutex> lock(taskQueMtx_);
			
			std::cout << "tid" << std::this_thread::get_id() << "尝试获取任务..." << std::endl;

			// cached模式下，有可能已经创建了很多的线程，但是空闲时间超过60s， 应该把多余的线程回收
			// 超过initThreadSize_数量的线程要进行回收
			// 当前时间 - 上一次线程执行的时间 > 60s 
			// 每一秒中返回一次 
			// 锁+双重判断
			while (isPoolRunning_ && taskQue_.size() == 0)
			{
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

				// 线程池结束，回收资源
				//if (!isPoolRunning_)
				//{
				//	threads_.erase(threadid);
				//	exitCond_.notify_all();
				//	std::cout << "threadid:" << std::this_thread::get_id() << "exit" << std::endl;
				//	return;
				//}
					
			}

			if (!isPoolRunning_)
			{
				break;
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
			task->exec();
		}
		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now();	// 更新线程执行完任务的时间
	}

	// 线程池结束
	threads_.erase(threadid);
	exitCond_.notify_all();
	std::cout << "threadid:" << std::this_thread::get_id() << "exit" << std::endl;
}

bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}

/////////////////////////////////////////////////
// 线程方法实现
int Thread::generateId_ = 0;

Thread::Thread(ThreadFunc func)
	:func_(func)
	,threadId_(generateId_++ )
{ 
}

Thread::~Thread()
{
}

void Thread::start()
{
	std::thread t(func_, threadId_);
	t.detach();
}

int Thread::getId() const
{
	return threadId_;
}

/////////////////////////////
// Result方法实现

Result::Result(std::shared_ptr<Task> task, bool isValid)
	:task_(task)
	,isValid_(isValid)
{
	task_->setResult(this);
}

Any Result::get()
{
	if (!isValid_)
	{
		return "";
	}
	sem_.wait();	// task任务没有执行完，阻塞用户当前线程
	return std::move(any_);
}

void Result::setVal(Any any)
{
	this->any_ = std::move(any);	// 存储task的返回值
	sem_.post();					// 增加信号量资源
}

///////////////////////////////////
// Task方法实现

Task::Task()
	:result_(nullptr)
{
}

void Task::exec()
{
	if (result_ != nullptr)
	{
		result_->setVal(run());
	}
}

void Task::setResult(Result* res)
{
	result_ = res;
}
