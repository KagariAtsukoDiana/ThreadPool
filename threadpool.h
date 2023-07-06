#ifndef __THREADPOOL_H
#define __THREADPOOL_H

#include <vector>
#include <queue>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <functional>


enum class PoolMode
{
	MODE_FIXED,	//固定数量的线程
	MODE_CACHED	//线程数量可动态增长
};

//抽象任务类
class Task
{
public:
	virtual void run() = 0;
private:
};

//线程类
class Thread
{
public:
	//线程函数对象类型
	using ThreadFunc = std::function<void()>;

	Thread(ThreadFunc func);
	~Thread();

	//启动线程
	void start();
private:
	ThreadFunc func_;
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

	//给线程池提交任务 用户调用改接口，传入任务对象，消费任务
	void submitTask(std::shared_ptr<Task> sp);

	//开启线程池
	void start(int initThreadSize = 4);

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	//线程函数 线程池的所有线程从任务队列里面消费任务
	void threadFunc();
private:
	std::vector<std::unique_ptr<Thread>> threads_;	//线程列表
	int initThreadSize_;	//初始的线程数量

	std::queue<std::shared_ptr<Task>> taskQue_;	//任务队列
	std::atomic_int taskSize_;	//任务数量
	int taskQueMaxThreshHold_;	//任务队列数量上限阈值

	std::mutex taskQueMtx_;		//保证任务队列的线程安全
	std::condition_variable notFull_;	//表示任务队列不满
	std::condition_variable notEmpty_;	//表示任务队列不空

	PoolMode poolMode_;	//当前线程池的工作模式
};


#endif // !__THREADPOOL_H
