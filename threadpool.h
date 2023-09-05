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
	MODE_FIXED,	// �̶��������߳�
	MODE_CACHED // �߳������ɶ�̬����
};

//�߳���
class Thread
{
public:
	//�̺߳�����������
	using ThreadFunc = std::function<void(int)>;

	Thread(ThreadFunc func)
		:func_(func)
		, threadId_(generateId_++)
	{}
	~Thread() = default;

	//�����߳�
	void start()
	{
		std::thread t(func_, threadId_);
		t.detach();
	}

	// ��ȡ�߳�id
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

//�̳߳���
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
		// �ȴ��̳߳��������̷߳���
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		notEmpty_.notify_all();	// �������󣬱���notEmpty_.wait()���˻��ѵ��µ�����
		exitCond_.wait(lock, [&]() { return threads_.size() == 0; });
	}

	//�����̳߳صĹ���ģʽ
	void setMode(PoolMode mode)
	{
		if (checkRunningState())
			return;
		poolMode_ = mode;
	}

	//����task�������������ֵ
	void setTaskQueMaxThreshHold(int threshhold);

	// �����̳߳�cachedģʽ���߳���ֵ
	void setThreadSizeThreshHold(int threshhold);
	 
	//���̳߳��ύ���� �û����øýӿڣ��������������������
	//Result submitTask(std::shared_ptr<Task> sp);
	template<typename Func, typename... Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
	{
		//������񣬷����������
		using RType = decltype(func(args...));
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		std::future<RType> result = task->get_future();

		// ��ȡ��
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		// �ȴ���������п���
		if (!notFull_.wait_for(lock, std::chrono::seconds(1)
			, [&]()->bool {return taskQue_.size() < taskQueMaxThreshHold_; }))
		{
			//��ʾnotFull�ȴ�1s,������Ȼû������
			std::cerr << "task queue is full, submit task fail." << std::endl;
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType {return RType(); });
			(*task)();
			return task->get_future();
		}

		// ����п��࣬������������������
		//taskQue_.emplace(sp);
		taskQue_.emplace([task]() {(*task)(); });
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
		return result;
	}
	
	//�����̳߳�
	void start(int initThreadSize = std::thread::hardware_concurrency())
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

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	//�̺߳��� �̳߳ص������̴߳��������������������
	void threadFunc(int threadid)
	{
		auto lastTime = std::chrono::high_resolution_clock().now();

		for (;;)
		{
			Task task;
			{
				// ��ȡ��
				std::unique_lock<std::mutex> lock(taskQueMtx_);

				std::cout << "tid" << std::this_thread::get_id() << "���Ի�ȡ����..." << std::endl;

				// cachedģʽ�£��п����Ѿ������˺ܶ���̣߳����ǿ���ʱ�䳬��60s�� Ӧ�ðѶ�����̻߳���
				// ����initThreadSize_�������߳�Ҫ���л���
				// ��ǰʱ�� - ��һ���߳�ִ�е�ʱ�� > 60s 
				// ÿһ���з���һ�� 
				// ��+˫���ж�
				while (taskQue_.size() == 0)
				{
					if (!isPoolRunning_)
					{
						// �̳߳ؽ���
						threads_.erase(threadid);
						exitCond_.notify_all();
						std::cout << "threadid:" << std::this_thread::get_id() << "exit" << std::endl;
						return;
					}

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
				task();
			}
			idleThreadSize_++;
			lastTime = std::chrono::high_resolution_clock().now();	// �����߳�ִ���������ʱ��
		}
	}

	bool checkRunningState() const
	{
		return isPoolRunning_;
	}
private:
	//std::vector<std::unique_ptr<Thread>> threads_;	//�߳��б�
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;	 //�߳��б�
	int initThreadSize_;	//��ʼ���߳�����
	int threadSizeThreshHold_;	//�߳�����������ֵ
	std::atomic_int curThreadSize_;	// ��ǰ�̳߳����̵߳�������
	std::atomic_int idleThreadSize_;	// ��¼�����̵߳�����
	
	using Task = std::function<void()>;
	std::queue<Task> taskQue_;	//�������
	std::atomic_int taskSize_;	//��������
	int taskQueMaxThreshHold_;	//�����������������ֵ

	std::mutex taskQueMtx_;		//��֤������е��̰߳�ȫ
	std::condition_variable notFull_;	//��ʾ������в���
	std::condition_variable notEmpty_;	//��ʾ������в���
	std::condition_variable exitCond_;	//�ȴ��߳���Դȫ������

	PoolMode poolMode_;	//��ǰ�̳߳صĹ���ģʽ
	std::atomic_bool isPoolRunning_ = true;	//��ʶ�̳߳��Ƿ�����
};

#endif // !__THREADPOOL_H
