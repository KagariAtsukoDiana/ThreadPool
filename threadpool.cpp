#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>

const int TASK_MAX_THRESHHOLD = 1024;

ThreadPool::ThreadPool()
	:initThreadSize_(0)
	,taskSize_(0)
	,taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	,poolMode_(PoolMode::MODE_FIXED)
{
}

ThreadPool::~ThreadPool()
{
}

void ThreadPool::setMode(PoolMode mode)
{
	poolMode_ = mode;
}

void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
	taskQueMaxThreshHold_ = threshhold;
}

void ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
}

void ThreadPool::start(int initThreadSize)
{
	initThreadSize_ = initThreadSize;
	//创建线程对象
	for (int i = 0; i < initThreadSize_; i++)
	{
		threads_.emplace_back(new Thread(std::bind(&ThreadPool::threadFunc, this)));
	}
	//启动所有线程
	for (int i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start();
	}
}

void ThreadPool::threadFunc()
{
	std::cout << "begin threadFunc tid:" << std::this_thread::get_id() << std::endl;
	std::cout << "end threadFunc tid:" << std::this_thread::get_id() << std::endl;
}

/////////////////////////////////////////////////
//线程方法实现

Thread::Thread(ThreadFunc func)
	:func_(func)
{
}

Thread::~Thread()
{
}

void Thread::start()
{
	std::thread t(func_);
	t.detach();
}
