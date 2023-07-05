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
	// ��ȡ��
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	// �ȴ���������п���
	/*while (taskQue_.size() == taskQueMaxThreshHold_)
	{
		notFull_.wait(lock);
	}*/ 
	notFull_.wait(lock, [&]()->bool {return taskQue_.size() < taskQueMaxThreshHold_; });
	
	// ����п��࣬������������������
	taskQue_.emplace(sp);
	taskSize_++;
	// �·������񣬶��в�Ϊ�գ���notEmpty_�Ͻ���֪ͨ
	notEmpty_.notify_all();
}

void ThreadPool::start(int initThreadSize)
{
	initThreadSize_ = initThreadSize;
	//�����̶߳���
	for (int i = 0; i < initThreadSize_; i++)
	{
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this));
		threads_.emplace_back(std::move(ptr));
	}
	//���������߳�
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
//�̷߳���ʵ��

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
