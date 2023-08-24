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
 
// Any类: 可以接受任意数据类型
class Any
{
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	template<typename T>
	Any(T data) : base_(std::make_unique<Derive<T>>(data))
	{} 

	template<typename T>
	T cast_()
	{
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr)
		{
			throw "type is unmatch";
		}
		return pd->data_;
	}
	
private:
	// 基类类型
	class Base
	{
	public:
		virtual ~Base() = default;
	};
	// 派生类类型
	template<typename T>
	class Derive : public Base
	{
	public:
		Derive(T data) : data_(data)
		{}

		T data_;
	};

private:
	// 定义一个基类的指针
	std::unique_ptr<Base> base_;
};

// 信号量类
class Semaphore
{
public:
	Semaphore(int limit = 0)
		:resLimit_(limit)
	{}
	~Semaphore() = default;

	void wait()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
		resLimit_--;
	}

	void post() 
	{
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;    
		cond_.notify_all();
	}

private:
	int resLimit_;
	std::mutex mtx_;
	std::condition_variable cond_;
};

class Task;
class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;

	Any get();
	void setVal(Any any);
private:
	Any any_;	// 存储任务的返回值
	Semaphore sem_;	// 线程通信信号量
	std::shared_ptr<Task> task_;	// 指向对应获取返回值的任务对象
	std::atomic_bool isValid_;	// 返回值是否有效
};

enum class PoolMode
{
	MODE_FIXED,	// 固定数量的线程
	MODE_CACHED // 线程数量可动态增长
};

//抽象任务类
class Task
{
public:
	Task();
	~Task() = default; 

	void exec();
	void setResult(Result* res);

	virtual Any run() = 0;

private:
	Result* result_;
};

//线程类
class Thread
{
public:
	//线程函数对象类型
	using ThreadFunc = std::function<void(int)>;

	Thread(ThreadFunc func);
	~Thread();

	//启动线程
	void start();

	// 获取线程id
	int getId() const;
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;
};

//线程池类
class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();

	//设置线程池的工作模式
	void setMode(PoolMode mode);

	//设置task任务队列上限阈值
	void setTaskQueMaxThreshHold(int threshhold);

	// 设置线程池cached模式下线程阈值
	void setThreadSizeThreshHold(int threshhold);

	//给线程池提交任务 用户调用改接口，传入任务对象，消费任务
	Result submitTask(std::shared_ptr<Task> sp);

	//开启线程池
	void start(int initThreadSize = 4);

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	//线程函数 线程池的所有线程从任务队列里面消费任务
	void threadFunc(int threadid);

	bool checkRunningState() const;
private:
	//std::vector<std::unique_ptr<Thread>> threads_;	//线程列表
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;	 //线程列表
	int initThreadSize_;	//初始的线程数量
	int threadSizeThreshHold_;	//线程数量上限阈值
	std::atomic_int curThreadSize_;	// 当前线程池里线程的总数量
	std::atomic_int idleThreadSize_;	// 记录空闲线程的数量
	 
	std::queue<std::shared_ptr<Task>> taskQue_;	//任务队列
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
