// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2018 baidu-rpc authors

#include <errno.h>
#include <bthread/bthread.h>
#include <butil/time.h>
#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include <brpc/rdma/block_pool.h>

class BlockPoolTest : public ::testing::Test {
protected:
    BlockPoolTest() { }
    ~BlockPoolTest() { }
};

#ifdef BRPC_RDMA
namespace brpc {
namespace rdma {
DECLARE_int32(rdma_memory_pool_initial_size_mb);
DECLARE_int32(rdma_memory_pool_increase_size_mb);
DECLARE_int32(rdma_memory_pool_max_regions);
DECLARE_int32(rdma_memory_pool_buckets);
extern void DestroyBlockPool();
extern int GetBlockType(void* buf);
extern size_t GetBlockSize(int type);
extern size_t GetGlobalLen(int block_type);
extern size_t GetRegionNum();
}
}

using namespace brpc::rdma;

static uint32_t DummyCallback(void*, size_t) {
    return 1;
}

TEST_F(BlockPoolTest, single_thread) {
    FLAGS_rdma_memory_pool_initial_size_mb = 1024;
    FLAGS_rdma_memory_pool_increase_size_mb = 1024;
    FLAGS_rdma_memory_pool_max_regions = 16;
    EXPECT_TRUE(InitBlockPool(DummyCallback) != NULL);

    size_t num = 4096;
    if (num > 1024 * 1024 * 1024 / GetBlockSize(0)) {
        num = 1024 * 1024 * 1024 / GetBlockSize(0);
    }
    void* buf[num];
    for (size_t i = 0; i < num; ++i) {
        buf[i] = AllocBlock(8192);
        EXPECT_TRUE(buf[i] != NULL);
        EXPECT_EQ(0, GetBlockType(buf[i]));
        EXPECT_EQ(0, GetBlockType(buf[i]));
    }
    for (size_t i = 0; i < num; ++i) {
        DeallocBlock(buf[i]);
        buf[i] = NULL;
    }
    for (size_t i = 0; i < num; ++i) {
        buf[i] = AllocBlock(GetBlockSize(0) + 1);
        EXPECT_TRUE(buf[i] != NULL);
        EXPECT_EQ(1, GetBlockType(buf[i]));
    }
    for (int i = num - 1; i >= 0; --i) {
        DeallocBlock(buf[i]);
        buf[i] = NULL;
    }
    for (size_t i = 0; i < num; ++i) {
        buf[i] = AllocBlock(GetBlockSize(3));
        EXPECT_TRUE(buf[i] != NULL);
        EXPECT_EQ(3, GetBlockType(buf[i]));
    }
    for (int i = num - 1; i >= 0; --i) {
        DeallocBlock(buf[i]);
        buf[i] = NULL;
    }

    DestroyBlockPool();
}

static void* AllocAndDealloc(void* arg) {
    uintptr_t i = (uintptr_t)arg;
    int len = GetBlockSize(i % 4);
    int iterations = 1000;
    while (iterations > 0) {
        void* buf = AllocBlock(len);
        EXPECT_TRUE(buf != NULL);
        EXPECT_EQ(i % 4, GetBlockType(buf));
        DeallocBlock(buf);
        --iterations;
    }
    return NULL;
}

TEST_F(BlockPoolTest, multiple_thread) {
    FLAGS_rdma_memory_pool_initial_size_mb = 8192;
    FLAGS_rdma_memory_pool_increase_size_mb = 8192;
    EXPECT_TRUE(InitBlockPool(DummyCallback) != NULL);

    uintptr_t thread_num = 32;
    bthread_t tid[thread_num];
    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    uint64_t start_time = butil::cpuwide_time_us();
    for (uintptr_t i = 0; i < thread_num; ++i) {
        ASSERT_EQ(0, bthread_start_background(&tid[i], &attr, AllocAndDealloc, (void*)i));
    }
    for (uintptr_t i = 0; i < thread_num; ++i) {
        ASSERT_EQ(0, bthread_join(tid[i], 0));
    }
    LOG(INFO) << "Total time = " << butil::cpuwide_time_us() - start_time << "us";

    DestroyBlockPool();
}

TEST_F(BlockPoolTest, extend) {
    FLAGS_rdma_memory_pool_initial_size_mb = 64;
    FLAGS_rdma_memory_pool_increase_size_mb = 64;
    FLAGS_rdma_memory_pool_buckets = 1;
    EXPECT_TRUE(InitBlockPool(DummyCallback) != NULL);

    EXPECT_EQ(1, GetRegionNum());
    size_t num = 4096;
    if (num > 1024 * 1024 * 1024 / GetBlockSize(0)) {
        num = 1024 * 1024 * 1024 / GetBlockSize(0);
    }
    void* buf[num];
    for (size_t i = 0; i < num; ++i) {
        buf[i] = AllocBlock(65534);
        EXPECT_TRUE(buf[i] != NULL);
    }
#ifdef IOBUF_HUGE_BLOCK
    EXPECT_EQ(FLAGS_rdma_memory_pool_max_regions, GetRegionNum());
#else
    EXPECT_EQ(5, GetRegionNum());
#endif
    for (size_t i = 0; i < num; ++i) {
        DeallocBlock(buf[i]);
    }
#ifdef IOBUF_HUGE_BLOCK
    EXPECT_EQ(FLAGS_rdma_memory_pool_max_regions, GetRegionNum());
#else
    EXPECT_EQ(5, GetRegionNum());
#endif

    DestroyBlockPool();
    FLAGS_rdma_memory_pool_buckets = 4;
}

TEST_F(BlockPoolTest, memory_not_enough) {
    FLAGS_rdma_memory_pool_initial_size_mb = 64;
    FLAGS_rdma_memory_pool_increase_size_mb = 64;
    FLAGS_rdma_memory_pool_buckets = 1;
    EXPECT_TRUE(InitBlockPool(DummyCallback) != NULL);

    EXPECT_EQ(1, GetRegionNum());
    size_t num = 15360;
    if (num > 1024 * 1024 * 1024 / GetBlockSize(0)) {
        num = 1024 * 1024 * 1024 / GetBlockSize(0);
    }
    void* buf[num];
    for (size_t i = 0; i < num; ++i) {
        buf[i] = AllocBlock(65534);
        EXPECT_TRUE(buf[i] != NULL);
    }
    EXPECT_EQ(16, GetRegionNum());
    void* tmp = AllocBlock(65536);
    EXPECT_EQ(ENOMEM, errno);
    EXPECT_EQ(0, GetRegionId(tmp));
    for (size_t i = 0; i < num; ++i) {
        DeallocBlock(buf[i]);
    }
    EXPECT_EQ(16, GetRegionNum());

    DestroyBlockPool();
    FLAGS_rdma_memory_pool_buckets = 4;
}

TEST_F(BlockPoolTest, invalid_use) {
    FLAGS_rdma_memory_pool_initial_size_mb = 64;
    FLAGS_rdma_memory_pool_increase_size_mb = 64;
    EXPECT_TRUE(InitBlockPool(DummyCallback) != NULL);

    void* buf = AllocBlock(0);
    EXPECT_EQ(NULL, buf);
    EXPECT_EQ(EINVAL, errno);

    buf = AllocBlock(GetBlockSize(3) + 1);
    EXPECT_EQ(NULL, buf);
    EXPECT_EQ(EINVAL, errno);

    errno = 0;
    DeallocBlock(NULL);
    EXPECT_EQ(EINVAL, errno);

    DestroyBlockPool();
}

#else

TEST_F(BlockPoolTest, dummy) {
}

#endif

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    google::ParseCommandLineFlags(&argc, &argv, true);
    return RUN_ALL_TESTS();
}

