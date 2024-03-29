#include <glog/logging.h>
#include <stdio.h>
#include <stdlib.h>

#include "server/messages.h"
#include "server/master.h"
#include <unordered_map>
#include <map>
#include <list>
#include <queue>

#ifndef NUM_INITIAL_WORKERS
#define NUM_INITIAL_WORKERS  1
#endif

#ifndef NUM_THREADS
#define NUM_THREADS  24
#endif

#ifndef MAX_REQUESTS
#define MAX_REQUESTS 36
#endif
struct worker_state{
  bool worker_ready;
  int requests_processing;
  int project_idea_requests_processing;
};

static struct Master_state {

  // The mstate struct collects all the master node state into one
  // place.  You do not need to preserve any of the fields below, they
  // exist only to implement the basic functionality of the starter
  // code.

  bool server_ready;
  int max_num_workers;
  int num_pending_client_requests;
  int next_tag;

  Worker_handle my_worker[4];
  // std::map<int, Worker_handle> my_workers;
  std::unordered_map<int, int> compareprimes_newtag_origtag;
  std::unordered_map<int, int> compareprimes_origtag_numresposnses;
  std::unordered_map<int, int> compareprimes_newtag_responses;

  //primarily for count primes requests
  std::unordered_map<std::string, Response_msg> cached_responses;
  std::unordered_map<int, Request_msg> cached_requests;

  std::unordered_map<std::string, std::queue<int>> cached_requests_str;
  std::unordered_map<std::string, bool> cached_responses_processing;
  std::list<Worker_handle> my_workers;
  std::unordered_map<int, Client_handle> waiting_clients;
  
  std::unordered_map<Worker_handle, worker_state> worker_states;
  //separate Q for projectideas
  std::queue<Request_msg> projectIdeaReqQueue;
  //Q for all other requests
  std::queue<Request_msg> ReqQueue;
  int num_workers_active;
  int idle_threads;
  int num_pending_workers;

} mstate;


// Generate a valid 'countprimes' request dictionary from integer 'n'
static void create_computeprimes_req(Request_msg& req, int n) {
  std::ostringstream oss;
  oss << n;
  req.set_arg("cmd", "countprimes");
  req.set_arg("n", oss.str());
}


int get_num(int max_allowed,int i, bool projectidea, bool minimize){
  int num;
  if (projectidea)
  {
     num =mstate.worker_states[mstate.my_worker[i]].project_idea_requests_processing;
  }
  else{
     num =mstate.worker_states[mstate.my_worker[i]].requests_processing;
  }
  if (minimize) {
    
    return max_allowed-num;
  
  } else {
    if (num>=max_allowed)
    {
      return 0;
    } else {
      return num;
    }
  }
  

}
// returns the worker with least number of idle threads
std::pair<int, int> get_min(int max_allowed, bool projectidea=false, bool minimize =true){
  int min,worker;
   min = get_num( max_allowed,  0,  projectidea,  minimize);
   worker = 0;
  for (int i = 1; i < mstate.num_workers_active; i++) {
      int this_min = get_num( max_allowed,  i,  projectidea,  minimize);
      DLOG(INFO) << "Sched Status " << i << "\t"<< this_min << "\t"<< min;
      if (min > this_min || min <= 0) {
          if (this_min > 0) {
              min = this_min;
              worker = i;
          }
      }
  }
  
  
  return std::make_pair(min,worker);
}
// mstate.worker_states[mstate.my_worker[index]].requests_processing==0 before calling this
void kill_worker_node_wrapper(int index) {
  mstate.worker_states.erase(mstate.my_worker[index]);
  mstate.num_workers_active--;
  mstate.idle_threads-=NUM_THREADS;
  kill_worker_node( mstate.my_worker[index]);
  for (int i = index; i < mstate.num_workers_active; ++i)
  {
    mstate.my_worker[i]=mstate.my_worker[i+1];
  }

  
}

void request_new_worker_node_wrapper(int i){
  int tag = random();
  Request_msg req(tag);
  char buffer [50];
  mstate.num_pending_workers++;
  sprintf (buffer, "name %d", i);
  req.set_arg("name", buffer);
  DLOG(INFO) << "Calling dummyy add";
  request_new_worker_node(req);
}

Worker_handle* get_best_worker_handle(int tag, Request_msg worker_req){
    if (worker_req.get_arg("cmd") == "tellmenow")
    {
      return &mstate.my_worker[0];
    }
    int min,worker;
    if (worker_req.get_arg("cmd") == "projectidea")
    {
      std::pair<int, int> answer= get_min(1,true);
      min = answer.first;
      worker = answer.second;
      if (min <= 0) {
          if ((mstate.num_workers_active < mstate.max_num_workers) && (mstate.num_pending_workers ==0)) {
              DLOG(INFO) << "Adding NEW WORKER for PROJECT_IDEA tag: " << tag;
              request_new_worker_node_wrapper(mstate.num_workers_active);
          }
          DLOG(INFO) << "Adding project idea request to queue";
          mstate.projectIdeaReqQueue.push(worker_req);
          return NULL;
      }
    } else{
      std::pair<int, int> answer= get_min(NUM_THREADS,false);
      min = answer.first;
      worker = answer.second;
      
      //Assign work to worker with assignments below MAX_THREADS
      //changed this to worker with least busy worker
      DLOG(INFO) << "MIN WORK " << min;
      if (min <=0) {
          std::pair<int, int> answer= get_min(MAX_REQUESTS,false,false);
          min = answer.first;
          worker = answer.second;
          if (min<=3)
          {
            if ((mstate.num_workers_active < mstate.max_num_workers) && (mstate.num_pending_workers == 0)) {
                  DLOG(INFO) << "Adding NEW WORKER for tag: " << tag;
                  request_new_worker_node_wrapper(mstate.num_workers_active);
              }
          }
      }

      if (min <= 0) {
          
          DLOG(INFO) << "Adding requests to queue";
          mstate.ReqQueue.push(worker_req);
          return NULL;
      }
    }
    //Assign to queue
    
    DLOG(INFO) << "Sending request to " << worker;
    return &mstate.my_worker[worker];
}


void assign_request(int tag, Request_msg worker_req) {
  mstate.num_pending_client_requests++;
  //get best_worker_handle returns NULL if none possible
  Worker_handle* best_worker_handle = get_best_worker_handle(tag, worker_req);
  if (best_worker_handle!=NULL)
  {
    if (worker_req.get_arg("cmd")=="projectidea")
    {
      mstate.worker_states[*best_worker_handle].project_idea_requests_processing++;
    }
    mstate.worker_states[*best_worker_handle].requests_processing++;
    
    mstate.idle_threads--;
    DLOG(INFO) << "Routed request: " << worker_req.get_request_string() << std::endl;
    send_request_to_worker(*best_worker_handle, worker_req);

  }

}
// Implements logic required by compareprimes command via multiple
// calls to execute_work.  This function fills in the appropriate
// response.
static void execute_compareprimes(const Request_msg& req, int params[4]) {


    // else{
      int origtag = mstate.next_tag;
      mstate.compareprimes_origtag_numresposnses[origtag] =  0;
      
      for (int i=0; i<4; i++) {
        int tag_req = origtag+i+1;
        Request_msg dummy_req(tag_req);
        create_computeprimes_req(dummy_req, params[i]);
        mstate.compareprimes_newtag_origtag[tag_req] = origtag;
        assign_request(tag_req,dummy_req);
      }
}



void master_node_init(int max_workers, int& tick_period) {

  // set up tick handler to fire every 5 seconds. (feel free to
  // configure as you please)
  tick_period = 1;

  mstate.next_tag = 0;
  mstate.max_num_workers = max_workers;
  mstate.num_pending_client_requests = 0;
  mstate.num_workers_active =0;
  // don't mark the server as ready until the server is ready to go.
  // This is actually when the first worker is up and running, not
  // when 'master_node_init' returnes
  mstate.server_ready = false;

  // fire off a request for a new worker
  DLOG(INFO) << "Master starting up [" << max_workers << "]";
  mstate.num_pending_workers = 0;
  for (int i =0; i < NUM_INITIAL_WORKERS; i++) {
      request_new_worker_node_wrapper(i);
  }
  //int tag = random();
  //Request_msg req(tag);
  //req.set_arg("name", "my worker 0");
  //request_new_worker_node(req);
}

void handle_new_worker_online(Worker_handle worker_handle, int tag) {

  // 'tag' allows you to identify which worker request this response
  // corresponds to.  Since the starter code only sends off one new
  // worker request, we don't use it here.
  DLOG(INFO) << "Handling new worker "; 
  mstate.num_pending_workers--;
  int worker_num=mstate.num_workers_active++;
  mstate.my_worker[worker_num] = worker_handle;
  worker_state ws;
  ws.project_idea_requests_processing=0;
  ws.requests_processing=0;
  ws.worker_ready=true;
  mstate.worker_states[worker_handle]=ws;
  mstate.idle_threads+=NUM_THREADS;


  // Now that a worker is booted, let the system know the server is
  // ready to begin handling client requests.  The test harness will
  // now start its timers and start hitting your server with requests.
  //if (mstate.num_workers_active==mstate.max_num_workers && mstate.server_ready == false) {
  DLOG(INFO) << "Active Workers " << mstate.num_workers_active; 
  if (mstate.num_workers_active > 0 && mstate.server_ready == false) {
    DLOG(INFO) << "Starting Server"; 
    server_init_complete();
    mstate.server_ready = true;
  }
  if (mstate.projectIdeaReqQueue.size()) {
      DLOG(INFO) << "Popping requests from project ideas queue for new worker";
      Request_msg worker_req = mstate.projectIdeaReqQueue.front();
      assign_request(worker_req.get_tag(), worker_req);
      mstate.projectIdeaReqQueue.pop();
  }
  int count = mstate.projectIdeaReqQueue.size();
  while (count--) {
      DLOG(INFO) << "Popping projectidea requests from queue for new worker";
      Request_msg worker_req = mstate.projectIdeaReqQueue.front();
      assign_request(worker_req.get_tag(), worker_req);
      mstate.projectIdeaReqQueue.pop();
  }
  count = mstate.ReqQueue.size();
  while (count--) {
      DLOG(INFO) << "Popping requests from queue for new worker";
      Request_msg worker_req = mstate.ReqQueue.front();
      assign_request(worker_req.get_tag(), worker_req);
      mstate.ReqQueue.pop();
  }
}

void handle_worker_response(Worker_handle worker_handle, const Response_msg& resp) {

  // Master node has received a response from one of its workers.
  // Here we directly return this response to the client.

  DLOG(INFO) << "Master received a response from a worker: [" << resp.get_tag() << ":" << resp.get_response() << "]" << std::endl;
  if (mstate.cached_requests.count(resp.get_tag()) && mstate.cached_requests[resp.get_tag()].get_arg("cmd")=="projectidea")
  {
    mstate.worker_states[worker_handle].project_idea_requests_processing--;
  }
  mstate.worker_states[worker_handle].requests_processing--;
  mstate.num_pending_client_requests--;
  mstate.idle_threads++;
  if (mstate.compareprimes_newtag_origtag.count(resp.get_tag()))
  {
    int origtag = mstate.compareprimes_newtag_origtag[resp.get_tag()];
    mstate.compareprimes_origtag_numresposnses[origtag]++;
    mstate.compareprimes_newtag_responses[resp.get_tag()] = atoi(resp.get_response().c_str());
    if (mstate.compareprimes_origtag_numresposnses[origtag]==4)
    {
      int counts[4];
      for (int i = 0; i <4 ; ++i)
      {
        counts[i]=mstate.compareprimes_newtag_responses[origtag+i+1];
        DLOG(INFO) << "counts [" << i << ":" << counts[i] << "]" << std::endl;
      }



      Response_msg new_resp(0);
      if (counts[1]-counts[0] > counts[3]-counts[2])
        new_resp.set_response("There are more primes in first range.");
      else
        new_resp.set_response("There are more primes in second range.");
      send_client_response(mstate.waiting_clients[origtag], new_resp);
    }
  } else{
    send_client_response(mstate.waiting_clients[resp.get_tag()], resp);
    std::string req_str = mstate.cached_requests[resp.get_tag()].get_request_string();
    mstate.cached_responses[req_str] =resp;

    if (mstate.cached_requests_str.count(req_str) && mstate.cached_requests_str[req_str].size())
    {

      while(mstate.cached_requests_str[req_str].size()) {
          int tag_req = mstate.cached_requests_str[req_str].front();
          send_client_response(mstate.waiting_clients[tag_req], resp);
          mstate.cached_requests_str[req_str].pop();
      }
    }
  }


  if (mstate.projectIdeaReqQueue.size() && mstate.cached_requests.count(resp.get_tag()) && mstate.cached_requests[resp.get_tag()].get_arg("cmd")=="projectidea")
  {
      DLOG(INFO) << "Popping requests from project ideas queue";
      Request_msg worker_req = mstate.projectIdeaReqQueue.front();
      assign_request(worker_req.get_tag(), worker_req);
      mstate.projectIdeaReqQueue.pop();
  }
  else if (mstate.ReqQueue.size()) {
      DLOG(INFO) << "Popping requests from queue";
      Request_msg worker_req = mstate.ReqQueue.front();
      assign_request(worker_req.get_tag(), worker_req);
      mstate.ReqQueue.pop();
  }

}

void handle_client_request(Client_handle client_handle, const Request_msg& client_req) {

  DLOG(INFO) << "Received request: " << client_req.get_request_string() << std::endl;



  // You can assume that traces end with this special message.  It
  // exists because it might be useful for debugging to dump
  // information about the entire run here: statistics, etc.
  if (client_req.get_arg("cmd") == "lastrequest") {
    Response_msg resp(0);
    resp.set_response("ack");
    send_client_response(client_handle, resp);
    return;
  }
  
  
  if (client_req.get_arg("cmd") == "compareprimes")
  {
    
    int params[4];
    // Response_msg resp(0);
    // grab the four arguments defining the two ranges
    params[0] = atoi(client_req.get_arg("n1").c_str());
    params[1] = atoi(client_req.get_arg("n2").c_str());
    params[2] = atoi(client_req.get_arg("n3").c_str());
    params[3] = atoi(client_req.get_arg("n4").c_str());
    Response_msg resp(0);
    //naive cases
    if (params[0]<=params[2]&& params[1]>= params[3]){
      resp.set_response("There are more primes in first range.");
      send_client_response(client_handle, resp);
      return;
    } else if(params[2]<=params[0]&& params[3]>= params[1]){
      resp.set_response("There are more primes in second range.");
      send_client_response(client_handle, resp);
      return;
    } else{
      mstate.waiting_clients[mstate.next_tag] = client_handle;
      execute_compareprimes(client_req, params);
      mstate.next_tag+=5;

      return;
    }
  }
  


  // The provided starter code cannot handle multiple pending client
  // requests.  The server returns an error message, and the checker
  // will mark the response as "incorrect"
  // if (mstate.num_pending_client_requests > 5000) {
  //   Response_msg resp(0);
  //   resp.set_response("Oh no! This server cannot handle multiple outstanding requests!");
  //   send_client_response(client_handle, resp);
  //   return;
  // }

  // Save off the handle to the client that is expecting a response.
  // The master needs to do this it can response to this client later
  // when 'handle_worker_response' is called.
  //mstate.waiting_client[tag] = client_handle;
  

  // Fire off the request to the worker.  Eventually the worker will
  // respond, and your 'handle_worker_response' event handler will be
  // called to forward the worker's response back to the server.
  

  if (mstate.cached_responses.count(client_req.get_request_string()))
  {
    Response_msg resp(0);
    resp.set_response(mstate.cached_responses[client_req.get_request_string()].get_response());
    send_client_response(client_handle, resp);

  } else {
    // if (mstate.cached_requests)
    // {
    //   /* code */
    // }
    int tag = mstate.next_tag++;
    mstate.waiting_clients[tag] = client_handle;
    mstate.cached_requests[tag] = client_req;
    if (mstate.cached_responses_processing.count(client_req.get_request_string()))
    {
      mstate.cached_requests_str[client_req.get_request_string()].push(tag);
    }
    else{
      mstate.cached_responses_processing[client_req.get_request_string()]=true;
      Request_msg worker_req(tag, client_req);
      assign_request(tag,worker_req);
    }// We're done!  This event handler now returns, and the master
    // process calls another one of your handlers when action is
    // required.
  }
}


void handle_tick() {

  // TODO: you may wish to take action here.  This method is called at
  // fixed time intervals, according to how you set 'tick_period' in
  // 'master_node_init'.
  // for(auto const &ent1 : mstate.) {
  //   // ent1.first is the first key
  //   for(auto const &ent2 : ent1.second) {
  //     // ent2.first is the second key
  //     // ent2.second is the data
  //   }
  // }
  DLOG(INFO) << "TICK ";
  for (int index = 0; index < mstate.num_workers_active; index++) {
      if (mstate.worker_states[mstate.my_worker[index]].requests_processing==0 && mstate.num_workers_active>1) {
          DLOG(INFO) << "KILLING WORKER " << index;
          kill_worker_node_wrapper(index);
          //Only kill one worker as index might be corrupted
          break;
      }
  }

}
