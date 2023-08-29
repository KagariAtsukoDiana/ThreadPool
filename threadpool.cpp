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
	// �ȴ��̳߳��������̷߳���
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();	// �������󣬱���notEmpty_.wait()���˻��ѵ��µ�����
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
	// ��ȡ��
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	// �ȴ���������п���
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

	// cachedģʽ �������������Ϳ����̵߳��������ж��Ƿ���Ҫ�����µ��̳߳��� ������С���������
	if (poolMode_ == PoolMode::MODE_CACHED
		&& taskSize_ > idleThreadSize_
		&& curThreadSize_ < threadSizeThreshHold_)
	{
		std::cout << "create new thread..." << std::endl;
		// �������߳�
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		// �����߳�
		threads_[threadId]->start();
		// �޸��̸߳�����صı���
		curThreadSize_++;
		idleThreadSize_++; 
	}

	// ���������Result����
	return Result(sp);
}

void ThreadPool::start(int initThreadSize)
{
	//�����̳߳ص�����״̬
	isPoolRunning_ = true;
	//��¼�̳߳�ʼ����
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;
	//�����̶߳���
	for (int i = 0; i < initThreadSize_; i++)
	{
		// ����thread�̶߳����ʱ�򣬰��̺߳�����thread�̶߳���
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
	}
	//���������߳�
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
			// ��ȡ��
			std::unique_lock<std::mutex> lock(taskQueMtx_);
			
			std::cout << "tid" << std::this_thread::get_id() << "���Ի�ȡ����..." << std::endl;

			// cachedģʽ�£��п����Ѿ������˺ܶ���̣߳����ǿ���ʱ�䳬��60s�� Ӧ�ðѶ�����̻߳���
			// ����initThreadSize_�������߳�Ҫ���л���
			// ��ǰʱ�� - ��һ���߳�ִ�е�ʱ�� > 60s 
			// ÿһ���з���һ�� 
			// ��+˫���ж�
			while (isPoolRunning_ && taskQue_.size() == 0)
			{
				if (poolMode_ == PoolMode::MODE_CACHED)
				{
					// ����������ʱ����
					if (std::cv_status::timeout
						== notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_)
						{
							// ���յ�ǰ�߳�
							// ��¼�߳���������ر�����ֵ�޸�
							// ���̶߳�����߳��б�������ɾ��
							// threadid -> �̶߳��� -> ɾ��
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
					// �ȴ�notEmpty����
					notEmpty_.wait(lock);
				}

				// �̳߳ؽ�����������Դ
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

			std::cout << "tid" << std::this_thread::get_id() << "��ȡ����ɹ�..." << std::endl;
			
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
		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now();	// �����߳�ִ���������ʱ��
	}

	// �̳߳ؽ���
	threads_.erase(threadid);
	exitCond_.notify_all();
	std::cout << "threadid:" << std::this_thread::get_id() << "exit" << std::endl;
}

bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}

/////////////////////////////////////////////////
// �̷߳���ʵ��
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
