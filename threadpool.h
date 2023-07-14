#ifndef __THREADPOOL_H
#define __THREADPOOL_H

#include <vector>
#include <queue>
#include <memory>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <functional>
 
// Any����: ���Խ���������������
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
		Derive<T>* pd = dynamic_cast<Derive<T>>(base_.get());
		if (pd == true)
		{
			throw "type is unmatch";
		}
		return pd->data_;
	}


private:
	// ��������
	class Base
	{
	public:
		virtual ~Base() = default;
	};
	// ����������
	template<typename T>
	class Derive : public Base
	{
	public:
		Derive(T data) : data_(data)
		{}

		T data_;
	};

private:
	// ����һ�������ָ��
	std::unique_ptr<Base> base_;
};

enum class PoolMode
{
	MODE_FIXED,	// �̶��������߳�
	MODE_CACHED	// �߳������ɶ�̬����
};

//����������
class Task
{
public:
	virtual Any run() = 0;
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

/*
example:
ThreadPool pool;
pool.start(4);
class Mytask : public Task
{
public:
	void run() { //�̴߳���... }
}

pool.submitTask(std::make_shared<MyTask>());
*/

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
