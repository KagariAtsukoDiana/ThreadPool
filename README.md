# ThreadPool
example:
```
ThreadPool pool;
pool.start(4);
class Mytask : public Task
{
public:
	void run() { //线程代码... }
}

Result res = pool.submitTask(std::make_shared<MyTask>());
```
