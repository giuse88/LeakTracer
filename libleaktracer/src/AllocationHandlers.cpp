////////////////////////////////////////////////////////
//
// LeakTracer
// Contribution to original project by Erwin S. Andreasen
// site: http://www.andreasen.org/LeakTracer/
//
// Added by Michael Gopshtein, 2006
// mgopshtein@gmail.com
//
// Any comments/suggestions are welcome
//
////////////////////////////////////////////////////////
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include "MemoryTrace.hpp"
#include "LeakTracer_l.hpp"
#include <execinfo.h>
#include <map>
#include <tuple>
#include <ctime>
#include <chrono>
#include <string>
#include <sstream>
#include <unistd.h>
#include <mutex>

void* (*lt_malloc)(size_t size);
void  (*lt_free)(void* ptr);
void* (*lt_realloc)(void *ptr, size_t size);
void* (*lt_calloc)(size_t nmemb, size_t size);

long int unix_timestamp()
{
    time_t t = std::time(0);
    long int now = static_cast<long int> (t);
    return now;
}

std::string toString(void *ptr)
{
  std::stringstream ss;
  ss << ptr;
  return ss.str();
}

class Recorder
{

  public:
    void add(void * ptr, long long size, bool isArray)
    {
      void **array = new void*[10];
      backtrace(array, 10);
      std::string key = toString(ptr);
      records[key] = std::make_tuple(array, std::chrono::system_clock::now(), size, isArray);
    }

    void remove(void *ptr)
    {
      auto it = records.find(toString(ptr));
      if (it != records.end())
      {
        delete std::get<0>(it->second);
        records.erase (it);
      }
      else
        fprintf(stderr, "%p not found", ptr);
    }

  private:
    std::map<std::string, std::tuple<void **, std::chrono::system_clock::time_point, long long, bool>> records;
};

int alloc = open("new.csv", O_WRONLY | O_CREAT);
int dealoc = open("delete.csv", O_WRONLY | O_CREAT);
int on = 0;

std::mutex new_mutex;
std::mutex delete_mutext;

#define BT_BUF_SIZE 10

void record_allocation(void * ptr, long long size, bool isArray)
{
  if ( on )
  {
    std::lock_guard<std::mutex> guard(new_mutex);
    int nptrs;
    void *buffer[BT_BUF_SIZE];
    nptrs = backtrace(buffer, BT_BUF_SIZE);
    dprintf(alloc, "%ld,%p,%lld,%d,", unix_timestamp(), ptr, size, isArray);
    backtrace_symbols_fd(buffer, nptrs, alloc);
    dprintf(alloc, "\n");
  }
}

void record_dealocation(void * ptr,bool isArray)
{
  if (on)
  {
    std::lock_guard<std::mutex> guard(delete_mutext);
    int nptrs;
    void *buffer[BT_BUF_SIZE];
    nptrs = backtrace(buffer, BT_BUF_SIZE);
    dprintf(dealoc, "%ld,%p,%d,", unix_timestamp(), ptr, isArray);
    backtrace_symbols_fd(buffer, nptrs, dealoc);
    dprintf(alloc, "\n");
  }
}

void* operator new(size_t size) {
	void *p;
	leaktracer::MemoryTrace::Setup();
	p = LT_MALLOC(size);
	leaktracer::MemoryTrace::GetInstance().registerAllocation(p, size, false);
  record_allocation(p, size, false);
	return p;
}

void* operator new[] (size_t size) {
	void *p;
	leaktracer::MemoryTrace::Setup();
	p = LT_MALLOC(size);
	leaktracer::MemoryTrace::GetInstance().registerAllocation(p, size, true);
  record_allocation(p, size, true);
	return p;
}


void operator delete (void *p) {
	leaktracer::MemoryTrace::Setup();
	leaktracer::MemoryTrace::GetInstance().registerRelease(p, false);
  record_dealocation(p, false);
	LT_FREE(p);
}


void operator delete[] (void *p) {
	leaktracer::MemoryTrace::Setup();
	leaktracer::MemoryTrace::GetInstance().registerRelease(p, true);
  record_dealocation(p, true);
	LT_FREE(p);
}

/** -- libc memory operators -- **/

/* malloc
 * in some malloc implementation, there is a recursive call to malloc
 * (for instance, in uClibc 0.9.29 malloc-standard )
 * we use a InternalMonitoringDisablerThreadUp that use a tls variable to prevent several registration
 * during the same malloc
 */
void *malloc(size_t size)
{
	void *p;
	leaktracer::MemoryTrace::Setup();
	leaktracer::MemoryTrace::GetInstance().InternalMonitoringDisablerThreadUp();
	p = LT_MALLOC(size);
	leaktracer::MemoryTrace::GetInstance().InternalMonitoringDisablerThreadDown();
	leaktracer::MemoryTrace::GetInstance().registerAllocation(p, size, false);
  //record_allocation(p, size, false);
	return p;
}

void free(void* ptr)
{
	leaktracer::MemoryTrace::Setup();

	leaktracer::MemoryTrace::GetInstance().registerRelease(ptr, false);
	LT_FREE(ptr);
  //record_dealocation(ptr, false);
}

void* realloc(void *ptr, size_t size)
{
	void *p;
	leaktracer::MemoryTrace::Setup();

	leaktracer::MemoryTrace::GetInstance().InternalMonitoringDisablerThreadUp();

	p = LT_REALLOC(ptr, size);

	leaktracer::MemoryTrace::GetInstance().InternalMonitoringDisablerThreadDown();

	if (p != ptr)
	{
		if (ptr)
			leaktracer::MemoryTrace::GetInstance().registerRelease(ptr, false);
		leaktracer::MemoryTrace::GetInstance().registerAllocation(p, size, false);
	}
	else
	{
		leaktracer::MemoryTrace::GetInstance().registerReallocation(p, size, false);
	}

  //record_allocation(p, size, false);
	return p;
}

void* calloc(size_t nmemb, size_t size)
{
	void *p;
	leaktracer::MemoryTrace::Setup();

	leaktracer::MemoryTrace::GetInstance().InternalMonitoringDisablerThreadUp();
	p = LT_CALLOC(nmemb, size);
	leaktracer::MemoryTrace::GetInstance().InternalMonitoringDisablerThreadDown();
	leaktracer::MemoryTrace::GetInstance().registerAllocation(p, nmemb*size, false);

  //record_allocation(p, size, false);
	return p;
}
