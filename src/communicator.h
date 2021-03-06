/*
 * Copyright (c) 2019, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __COMMUNICATOR_CUH
#define __COMMUNICATOR_CUH

#include <vector>
#include <queue>
#include <ucp/api/ucp.h>
#include <mpi.h>
#include <cstdint>


#define comm_handle_t void*


class Communicator
{

public:

/**
 * Initialize the communicator. This method should be called by all ranks in MPI_COMM_WORLD. Also, each rank should
 * call this method at most once. In other word, multiple communicators are not supported.
 *
 * @param[in] argc      Number of arguments in argv.
 * @param[in] argv      Arguments passed to MPI_Init.
 */
virtual void initialize(int argc, char *argv[]) = 0;

/**
 * Send data to a remote rank asynchronously.
 *
 * @param[in] buf           Data buffer to send to remote rank
 * @param[in] count         Number of elements to send
 * @param[in] element_size  Size of each element
 * @param[in] dest          Destination rank
 * @param[in] tag           Message tag
 *
 * @returns                 Communication handle for waiting. See 'wait' and 'waitall'.
 */
virtual comm_handle_t send(const void *buf, int64_t count, int element_size, int dest, int tag) = 0;

/**
 * Receive data from a remote rank asynchronously. Use this version if the receive size is known.
 *
 * @param[in] buf           Receive buffer to place received data into
 * @param[in] count         Number of elements to receive
 * @param[in] element_size  Size of each element
 * @param[in] source        Source rank
 * @param[in] tag           Message tag
 *
 * @returns                 Communication handle for waiting. See 'wait' and 'waitall'.
 */
virtual comm_handle_t recv(void *buf, int64_t count, int element_size, int source, int tag) = 0;

/**
 * Receive data from a remote rank asynchronously. Use this version if the receive size is unknown.
 *
 * @param[out] buf          Allocated receive buffer. It is the caller's responsibility to free this buffer.
 * @param[out] count        Number of elements received
 * @param[in] element_size  The size of each element
 * @param[in] source        Source rank
 * @param[in] tag           Message tag
 *
 * @returns                 Communication handle for waiting. See 'wait' and 'waitall'.
 */
virtual comm_handle_t recv(void **buf, int64_t *count, int element_size, int source, int tag) = 0;

/**
 * The host process will block until a single communication is completed.
 *
 * @param[in] request       Communication handle to wait for
 */
virtual void wait(comm_handle_t request) = 0;

/**
 * The host process will block until all communications specified are completed.
 *
 * @param[in] requests      A vector of communication handles to wait for
 */
virtual void waitall(std::vector<comm_handle_t> requests) = 0;

/**
 * The host process will block until all communications specified are completed.
 *
 * @param[in] begin         Iterator points to the first handle to wait for
 * @param[in] end           Iterator points to the next handle after the last
 */
virtual void waitall(std::vector<comm_handle_t>::const_iterator begin, std::vector<comm_handle_t>::const_iterator end) = 0;

/**
 * Close the endpoints, free up used communication resources, and stop the communication runtime.
 */
virtual void finalize() = 0;

int mpi_rank;
int mpi_size;
int device_count;
int current_device;

};


class UCXCommunicator: public Communicator
{

public:

virtual void initialize(int argc, char *argv[]);
virtual comm_handle_t send(const void *buf, int64_t count, int element_size, int dest, int tag);
virtual comm_handle_t recv(void *buf, int64_t count, int element_size, int source, int tag);
virtual comm_handle_t recv(void **buf, int64_t *count, int element_size, int source, int tag);
virtual void wait(comm_handle_t request);
virtual void waitall(std::vector<comm_handle_t> requests);
virtual void waitall(std::vector<comm_handle_t>::const_iterator begin, std::vector<comm_handle_t>::const_iterator end);
virtual void finalize();

ucp_context_h         ucp_context;
ucp_worker_h          ucp_worker;
ucp_address_t         *ucp_worker_address;
size_t                ucp_worker_address_len;
std::vector<ucp_ep_h> ucp_endpoints;

private:

virtual void initialize_mpi(int argc, char *argv[]);
virtual void initialize_ucx();
virtual void create_endpoints();

/**
 * This helper function selects the CUDA device and sets up RMM memory pool.
 */
virtual void initialize_cuda();

};


class UCXBufferCommunicator: public UCXCommunicator
{

public:

virtual void initialize(int argc, char *argv[]);

/**
 * Allocate communication buffers and put them into the 'buffer_cache' queue.
 *
 * @param[in] ncaches       Total number of communication buffers.
 * @param[in] cache_size    Size of each communication buffer.
 */
virtual void setup_cache(int64_t ncaches, int64_t cache_size);

/**
 * Register the communication buffer.
 *
 * Note: To use this interface, each rank must have the same number of communication buffers.
 */
virtual void warmup_cache();

virtual comm_handle_t send(const void *buf, int64_t count, int element_size, int dest, int tag);
virtual comm_handle_t recv(void *buf, int64_t count, int element_size, int source, int tag);
virtual comm_handle_t recv(void **buf, int64_t *count, int element_size, int source, int tag);
virtual void wait(comm_handle_t request);
virtual void waitall(std::vector<comm_handle_t> requests);
virtual void waitall(std::vector<comm_handle_t>::const_iterator begin, std::vector<comm_handle_t>::const_iterator end);
virtual void finalize();

enum CommInfoTypes { SEND, RECV };

struct CommInfo
{
    CommInfoTypes types;
    bool completed;  // used only for the handle returned to the user to signal completion
    UCXBufferCommunicator *comm;  // pointer to the Communicator object
    CommInfo *orig_info;  // pointer to the handle returned to the user
    void *comm_buffer;  // communication buffer
    bool custom_allocated;  // indicate whether this object is allocated by UCX or communicator
};

struct SendInfo
{
    CommInfoTypes types;
    bool completed;
    UCXBufferCommunicator *comm;
    CommInfo *orig_info;
    void *comm_buffer;
    bool custom_allocated;
    const void *send_buffer;  // user buffer
    int64_t *count;  // pointer to the total number of elements need to be sent
    int element_size;  // the size of each element
    int dest;  // destination rank
    int user_tag;  // tag specified by the user
    int ibatch;  // current batch number
};

struct RecvInfo
{
    CommInfoTypes types;
    bool completed;
    UCXBufferCommunicator *comm;
    CommInfo *orig_info;
    void *comm_buffer;
    bool custom_allocated;
    void **recv_buffer; // pointer to the receive buffer
    int64_t *count;  // pointer to the total number of elements need to be received
    int element_size;  // the size of each element
    int source;  // source rank
    int user_tag;  // tag specified by the user
    int ibatch;  // current batch number
};


int64_t comm_buffer_size;
std::queue<void *> buffer_cache;
cudaStream_t copy_stream;  // stream used for copying between user buffer and communication buffer

private:

virtual void initialize_ucx();
bool wait_send(SendInfo *info);
bool wait_recv(RecvInfo *info);
comm_handle_t recv_helper(void **buf, int64_t *count, int element_size, int source, int tag);

void *cache_start_addr;

};


#endif // __COMMUNICATOR_CUH
