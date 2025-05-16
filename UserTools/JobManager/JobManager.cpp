#include "JobManager.h"

JobManager::JobManager():Tool(){}


bool JobManager::Initialise(std::string configfile, DataModel &data){
  
  InitialiseTool(data);
  m_configfile = configfile;
  InitialiseConfiguration(configfile);
  //m_variables.Print();
  LoadConfig();
  
  bool self_serving =true;
  m_variables.Get("self_serving", self_serving);
  
  m_data->thread_num=0;
  
  worker_pool_manager= new WorkerPoolManager(m_data->job_queue, &m_thread_cap, &(m_data->thread_cap), &(m_data->thread_num), nullptr, self_serving);
  
  
  ExportConfiguration();
  
  return true;
}


bool JobManager::Execute(){
  
  if(m_data->change_config){
    InitialiseConfiguration(m_configfile);  
    LoadConfig();
    ExportConfiguration();
  }
  
  m_data->monitoring_store_mtx.lock();
  m_data->monitoring_store.Set("pool_threads",worker_pool_manager->NumThreads());
  m_data->monitoring_store.Set("queued_jobs",m_data->job_queue.size());
   m_data->monitoring_store_mtx.unlock();
   //  printf("jobmanager q:t = %d:%d\n", m_data->job_queue.size(), worker_pool_manager->NumThreads());
  usleep(1000);
  if(worker_pool_manager->NumThreads()==m_thread_cap)  m_data->services->SendLog("Warning: Worker Pool Threads Maxed" , 0); //make this a warning
  // std::cout<<"NumThreads="<<worker_pool_manager->NumThreads()<<std::endl;
  //sleep(1);
  
  return true;
}


bool JobManager::Finalise(){
  bool wait;
  if (m_variables.Get("wait_for_jobs", wait) && wait)
    while (m_data->job_queue.size())
      usleep(10000);
  
  delete worker_pool_manager;
  worker_pool_manager=0;
  
  return true;
}

void JobManager::LoadConfig(){
  
  if(!m_variables.Get("verbose",m_verbose)) m_verbose=1;
  if(!m_variables.Get("thread_cap",m_thread_cap)) m_thread_cap=20;
  if(!m_variables.Get("global_thread_cap",m_data->thread_cap)) m_data->thread_cap=20; 
  //m_thread_cap=200;
  //m_data->thread_cap=200;
}
