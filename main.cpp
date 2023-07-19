#include <iostream>
#include <chrono>
#include <thread>

#include "threadpool.h"

using ULONG = unsigned long long;

class MyTask :public Task
{
public:
	MyTask(int begin, int end)
		:begin_(begin)
		,end_(end)
	{}

	Any run()
	{
		std::cout << "tid:" << std::this_thread::get_id() << "begin!" << std::endl;
		ULONG sum = 0;
		for (ULONG i = begin_; i <= end_; i++)
		{
			sum += i;
		}
		std::cout << "tid:" << std::this_thread::get_id() << "end!" << std::endl;

		return sum;
	}
private:
	int begin_;
	int end_;
};

int main()
{
	ThreadPool pool;
	pool.start(4);
	//std::this_thread::sleep_for(std::chrono::seconds(5));

	Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
	Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
	Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

	ULONG sum1 = res1.get().cast_<ULONG>();
	ULONG sum2 = res2.get().cast_<ULONG>();
	ULONG sum3 = res3.get().cast_<ULONG>();

	std::cout << (sum1 + sum2 + sum3) << std::endl;

	ULONG sum = 0;
	for (ULONG i = 1; i <= 300000000; i++)
		sum += i;
	std::cout << sum << std::endl;

	return 0;
}