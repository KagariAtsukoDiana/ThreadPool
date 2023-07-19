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

Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	// ��ȡ��
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	// �ȴ���������п���
	/*
	while (taskQue_.size() == taskQueMaxThreshHold_)
	{
		notFull_.wait(lock);
	}
	*/
	if (!notFull_.wait_for(lock, std::chrono::seconds(1)
		, [&]()->bool {return taskQue_.size() < taskQueMaxThreshHold_; }))
	{
		//��ʾnotFull�ȴ�1s,������Ȼû������
		std::cerr << "task queue is full, submit task fail." << std::endl;
		return Result(sp, false);
	}
	
	// ����п��࣬������������������
	taskQue_.emplace(sp);
	taskSize_++;
	// �·������񣬶��в�Ϊ�գ���notEmpty_�Ͻ���֪ͨ
	notEmpty_.notify_all();
	return Result(sp);
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
	/*
	std::cout << "begin threadFunc tid:" << std::this_thread::get_id() << std::endl;
	std::cout << "end threadFunc tid:" << std::this_thread::get_id() << std::endl;
	*/
	for (;;)
	{
		std::shared_ptr<Task> task;
		{
			// ��ȡ��
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			// �ȴ�notEmpty����
			notEmpty_.wait(lock, [&]()->bool {return taskQue_.size() > 0; });

			// ���������ȡһ���������
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// ���������ʣ������ ����֪ͨ�����߳�ִ������
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}

			// ȡ��һ������֪ͨ���Լ����ύ��������
			notFull_.notify_all();
		}// ���������ͷ���

		// ��ǰ�̸߳���ִ���������
		if (task != nullptr)
		{
			task->exec();
		}
	}

}

/////////////////////////////////////////////////
// �̷߳���ʵ��

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

/////////////////////////////
// Result����ʵ��

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
	sem_.wait();	// task����û��ִ���꣬�����û���ǰ�߳�
	return std::move(any_);
}

void Result::setVal(Any any)
{
	this->any_ = std::move(any);	// �洢task�ķ���ֵ
	sem_.post();					// �����ź�����Դ
}

///////////////////////////////////
// Task����ʵ��

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
