#include <iostream>
#include "threadpool.h"

int sun1(int a, int b)
{
	return a + b;
}

int main()
{
	ThreadPool pool;
	pool.start(4);
	std::future<int> res = pool.submitTask(sun1, 1, 2);
	std::cout << res.get() << std::endl;
	return 0;
}