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
	MODE_FIXED,	//�̶��������߳�
	MODE_CACHED	//�߳������ɶ�̬����
};

//����������
class Task
{
public:
	virtual void run() = 0;
private:
};

//�߳���
class Thread
{
public:
	//�̺߳�����������
	using ThreadFunc = std::function<void()>;

	Thread(ThreadFunc func);
	~Thread();

	//�����߳�
	void start();
private:
	ThreadFunc func_;
};

//�̳߳���
class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();


	//�����̳߳صĹ���ģʽ
	void setMode(PoolMode mode);

	//����task�������������ֵ
	void setTaskQueMaxThreshHold(int threshhold);

	//���̳߳��ύ���� �û����øĽӿڣ��������������������
	void submitTask(std::shared_ptr<Task> sp);

	//�����̳߳�
	void start(int initThreadSize = 4);

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	//�̺߳��� �̳߳ص������̴߳��������������������
	void threadFunc();
private:
	std::vector<std::unique_ptr<Thread>> threads_;	//�߳��б�
	int initThreadSize_;	//��ʼ���߳�����

	std::queue<std::shared_ptr<Task>> taskQue_;	//�������
	std::atomic_int taskSize_;	//��������
	int taskQueMaxThreshHold_;	//�����������������ֵ

	std::mutex taskQueMtx_;		//��֤������е��̰߳�ȫ
	std::condition_variable notFull_;	//��ʾ������в���
	std::condition_variable notEmpty_;	//��ʾ������в���

	PoolMode poolMode_;	//��ǰ�̳߳صĹ���ģʽ
};


#endif // !__THREADPOOL_H
