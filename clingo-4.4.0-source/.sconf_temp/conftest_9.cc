
    #include <tbb/compat/condition_variable>
    int main(int argc, char **argv)
    {
        tbb::interface5::unique_lock<tbb::mutex> lock;
        tbb::tick_count::interval_t i;
        tbb::interface5::condition_variable cv;
        cv.wait_for(lock, i);
        return 0;
    }
    