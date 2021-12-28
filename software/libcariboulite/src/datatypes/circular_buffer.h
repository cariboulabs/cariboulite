#ifndef __SAMPLE_CIRC_BUFFER_H__
#define __SAMPLE_CIRC_BUFFER_H__

#include <string.h>
#include <thread>
#include <mutex>
#include <condition_variable>


#define IS_POWER_OF_2(x)  	(!((x) == 0) && !((x) & ((x) - 1)))
#define MIN(x,y)			((x)>(y)?(y):(x))

uint32_t next_power_of_2 (uint32_t x)
{
	uint32_t power = 1;
	while(power < x)
	{
		power <<= 1;
	}
	return power;
}

template <class T>
class circular_buffer {
public:
	circular_buffer(size_t size, bool override_write = true, bool block_read = true)
	{ 
		max_size_ = size;
		if (!IS_POWER_OF_2(max_size_))
		{
			max_size_ = next_power_of_2(max_size_);
		}
		buf_ = new T[max_size_];
		override_write_ = override_write;
		block_read_ = block_read;
	}

	~circular_buffer()
	{
		std::unique_lock<std::mutex> lock(mutex_);
		delete []buf_; 
	}

	size_t put(const T *data, size_t length)
	{
		std::lock_guard<std::mutex> lock(mutex_);

		if ((max_size_ - size()) < length && override_write_)
		{
			// pop the amount of data the is needed
			tail_ += length - (max_size_ - size());
		}

		size_t len = MIN(length, max_size_ - head_ + tail_);
		auto l = MIN(len, max_size_ - (head_  & (max_size_ - 1)));

		memcpy(buf_ + (head_ & (max_size_ - 1)), data, l * sizeof(T));
		memcpy(buf_, data + l, (len - l) * sizeof(T));
		
		head_ += len;

		if (block_read_) 
		{
      		cond_var_.notify_one();
    	}

		return len;
	}

	size_t get(T *data, size_t length)
	{
		std::unique_lock<std::mutex> lock(mutex_);

		if (block_read_)
		{	
      		cond_var_.wait(lock, [&]()
            {
                // Acquire the lock only if
                // we got enough items
                return size() >= length;
            });
    	}

		size_t len = MIN(length, head_ - tail_);
		auto l = MIN(len, max_size_ - (tail_ & (max_size_ - 1)));

		if (data != NULL)
		{
			memcpy(data, buf_ + (tail_ & (max_size_ - 1)), l * sizeof(T));
			memcpy(data + l, buf_, (len - l) * sizeof(T));
		}
		tail_ += len;
		return len;
	}

	void put(T item)
	{
		put(&item, 1);
	}

	T get()
	{
		T item;
		get(&item, 1);
		return item;
	}

	void reset()
	{
		std::unique_lock<std::mutex> lock(mutex_);
		head_ = tail_ = 0;
	}

	inline bool empty()
	{
		return head_ == tail_;
	}

	inline bool full()
	{
		return size() == capacity();
	}

	inline size_t capacity() const
	{
		return max_size_;
	}

	size_t size()
	{
		return (head_ - tail_);
	}

	void print_buffer()
	{
		std::unique_lock<std::mutex> lock(mutex_);
		size_t t = tail_;
		int i = 0;
		while (t < head_)
		{
			printf("%d => %d\n", i++, (int)buf_[t++&(max_size_-1)]);
		}
	}

private:
	std::mutex mutex_;
	std::condition_variable cond_var_;

	T* buf_;
	size_t head_ = 0;
	size_t tail_ = 0;
	size_t max_size_;
	bool override_write_;
	bool block_read_;
};

#endif //__SAMPLE_CIRC_BUFFER_H__