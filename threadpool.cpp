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
	// 获取锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	// 等待任务队列有空余
	/*while (taskQue_.size() == taskQueMaxThreshHold_)
	{
		notFull_.wait(lock);
	}*/
	if (!notFull_.wait_for(lock, std::chrono::seconds(1)
		, [&]()->bool {return taskQue_.size() < taskQueMaxThreshHold_; }))
	{
		//表示notFull等待1s,条件依然没有满足
		std::cerr << "task queue is full, submit task fail." << std::endl;
	}
	
	// 如果有空余，把任务放入任务队列中
	taskQue_.emplace(sp);
	taskSize_++;
	// 新放了任务，队列不为空，在notEmpty_上进行通知
	notEmpty_.notify_all();
}

void ThreadPool::start(int initThreadSize)
{
	initThreadSize_ = initThreadSize;
	//创建线程对象
	for (int i = 0; i < initThreadSize_; i++)
	{
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this));
		threads_.emplace_back(std::move(ptr));
	}
	//启动所有线程
	for (int i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start();
	}
}

void ThreadPool::threadFunc()
{
	/*std::cout << "begin threadFunc tid:" << std::this_thread::get_id() << std::endl;
	std::cout << "end threadFunc tid:" << std::this_thread::get_id() << std::endl;*/
	for (;;)
	{
		std::shared_ptr<Task> task;
		{
			// 获取锁
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			// 等待notEmpty条件
			notEmpty_.wait(lock, [&]()->bool {return taskQue_.size() > 0; });

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
			task->run();
		}
	}

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
