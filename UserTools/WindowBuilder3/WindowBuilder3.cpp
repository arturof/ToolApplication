#include "WindowBuilder3.h"

WindowBuilder3_args::WindowBuilder3_args():Thread_args(){}

WindowBuilder3_args::~WindowBuilder3_args(){}

BuildWindow_args::BuildWindow_args():Thread_args(){}

BuildWindow_args::~BuildWindow_args(){}

WindowBuilder3::WindowBuilder3():Tool(){}


bool WindowBuilder3::Initialise(std::string configfile, DataModel &data){

  InitialiseTool(data);
  m_configfile=configfile;
  InitialiseConfiguration(configfile);
  //m_variables.Print();


  m_util=new Utilities();
  args=new WindowBuilder3_args();
  LoadConfig();
  args->data=m_data;
  if(m_data->readout_windows==0) m_data->readout_windows=new std::deque<ReadoutWindow*>;
  m_util->CreateThread("test", &Thread, args);

  
  ExportConfiguration();
  
  return true;
}


bool WindowBuilder3::Execute(){

  if(m_data->change_config){
    InitialiseConfiguration(m_configfile);
    LoadConfig();
    ExportConfiguration();
  }

  //  //printf("triggered_data::readout_windows = %d::%d\n", m_data->triggered_data.size(), m_data->readout_windows->size());
  // print somthing with it  args->readout_num;
  usleep(100);
  return true;
}


bool WindowBuilder3::Finalise(){

  m_util->KillThread(args);

  delete args;
  args=0;

  delete m_util;
  m_util=0;

  return true;
}

void WindowBuilder3::Thread(Thread_args* arg){

  WindowBuilder3_args* args=reinterpret_cast<WindowBuilder3_args*>(arg);
  //printf("d1\n");
  std::vector<unsigned int> erase_list;
  //printf("d2 size=%d\n",args->data->triggered_data.size());
  //std::map<unsigned int, MPMTData*> triggered_data;
  args->data->triggered_data_mtx.lock();
  args->triggered_data.insert(args->data->triggered_data.begin(), args->data->triggered_data.end());
  args->data->triggered_data.clear();
  //std::swap(args->data->triggered_data.begin(),myints+7, myvector.begin());
  args->data->triggered_data_mtx.unlock();
   printf("triggered size=%lu\n", args->triggered_data.size());

   std::vector<std::shared_ptr<MPMTData> > shared_ptrs;
   /*  
  for(std::map<unsigned int, MPMTData*>::iterator it=args->triggered_data.begin(); it!=args->triggered_data.end(); it++){
   delete it->second;
    }
  args->triggered_data.clear();
  */
  
  for(std::map<unsigned int, MPMTData*>::iterator it=args->triggered_data.begin(); it!=args->triggered_data.end(); it++){
    //it->second->Print();
    //    printf("d3 %d:%d\n",it->first,(args->data->current_coarse_counter >>22) - 500);
    
    while( (it->first > (args->data->current_coarse_counter >>22) - 500) && it->first!=0 && (!args->triggered_data.count(it->first +1)  && !(args->triggered_data.count(it->first -1)))) usleep(100);

    
    bool orphaned=it->first < (args->data->current_coarse_counter >>22) - 500;
    bool first= it->first ==0;
    bool pre_cluster=(args->triggered_data.count(it->first -1));
    bool post_cluster= (args->triggered_data.count(it->first +1)); 
    
    //if(orphaned) printf("orphaned\n");
							       //    if( args->triggered_data.count(it->first +1)  && (args->triggered_data.count(it->first -1) || it->first ==0)  || orphaned){
      
       //printf("d4\n");
      //////////////////merging triggers//////////////////////////
      std::sort(it->second->unmerged_triggers.begin(), it->second->unmerged_triggers.end(), [](TriggerInfo a, TriggerInfo b){
	  return a.time < b.time;
	});
       //printf("d5\n");
      BuildWindow_args* job_args = new BuildWindow_args;
      
      //std::vector<std::vector<TriggerInfo*> >merged_triggers;
      //std::map<unsigned int, bool> trigger_veto;
       //printf("d6\n");
      ////////////// finding MAIN and LED primaries ////////////////
      for(unsigned int i=0; i<it->second->unmerged_triggers.size(); i++){
	 //printf("d7\n");
	if((it->second->unmerged_triggers.at(i).type==TriggerType::MAIN && args->offset_trigger.count(TriggerType::MAIN))|| (it->second->unmerged_triggers.at(i).type==TriggerType::LED && args->offset_trigger.count(TriggerType::LED))){
	  std::vector<TriggerInfo*> tmp;
	  tmp.push_back(&(it->second->unmerged_triggers.at(i)));
	  job_args->trigger_veto[i]=true;
	  job_args->merged_triggers.push_back(tmp);
	}
	
      }
							       
							       
      
      //     BuildWindow_args* job_args = new BuildWindow_args;
      job_args->data= args->data;
      job_args->pre_cluster=pre_cluster;
      job_args->post_cluster=post_cluster;

      if(!pre_cluster) shared_ptrs.emplace_back(args->triggered_data[it->first]);
      
      if(pre_cluster){
	job_args->triggered_data.push_back(shared_ptrs.back());
      }

      job_args->triggered_data.push_back(shared_ptrs.back());
      printf("out size=%lu\n", job_args->triggered_data.size());

      if(post_cluster){
	shared_ptrs.emplace_back(args->triggered_data[it->first+1]);	
	job_args->triggered_data.push_back(shared_ptrs.back());
      }

      for(int i=0; i<job_args->merged_triggers.size(); i++){
	if(job_args->merged_triggers.at(i).at(0)->type==TriggerType::MAIN){
	  job_args->tdc_hits = args->data->tdc_readout.getEvent();
	  job_args->qdc_hits = args->data->qdc_readout.getEvent();
	}
      }
	Job* tmp_job = new Job("windowbuilder");
	tmp_job->data=job_args;
	tmp_job->func=BuildWindow;
	printf("job pointer = %p\n",tmp_job);
	//args->data->job_queue.AddJob(tmp_job);
	delete tmp_job;
	delete job_args;
  
      if(orphaned) erase_list.push_back(it->first);
      else if(it->first !=0) erase_list.push_back(it->first - 1); 

  }
  /*
 //printf("d8\n");
      ///////////////////////////// assosiating others with primaries //////////////
      for(unsigned int i=0; i<it->second->unmerged_triggers.size(); i++){
	if(trigger_veto.count(i)) continue;
	for(unsigned int j=0; j<merged_triggers.size(); j++){
	  long offset=args->offset_trigger[merged_triggers.at(j).at(0)->type];
	  unsigned long pre=args->pre_trigger[merged_triggers.at(j).at(0)->type];;
	  unsigned long post=args->post_trigger[merged_triggers.at(j).at(0)->type];;
	  if(it->second->unmerged_triggers.at(i).time + args->offset_trigger[it->second->unmerged_triggers.at(i).type] > merged_triggers.at(j).at(0)->time + offset - pre 
	     &&
	     it->second->unmerged_triggers.at(i).time + args->offset_trigger[it->second->unmerged_triggers.at(i).type] < merged_triggers.at(j).at(0)->time + offset + post){
	    merged_triggers.at(j).push_back(&(it->second->unmerged_triggers.at(i)));
	    trigger_veto[i]=true;
	  }	  
	}
      }
      //////////////////////////////////////////////////////////////////////////////
       //printf("d9\n");
      ////////////////////////////// adding nhits and straglers (shouldnt be any other than nhits)
    
      if(args->offset_trigger.count(TriggerType::NHITS)){
	for(unsigned int i=0; i<it->second->unmerged_triggers.size(); i++){
	  if(trigger_veto.count(i)) continue;
	  std::vector<TriggerInfo*> tmp;
	  tmp.push_back(&(it->second->unmerged_triggers.at(i)));
	  trigger_veto[i]=true;
  //printf("d10\n");
	  for(unsigned int j=0; j<it->second->unmerged_triggers.size(); j++){
	    //printf("d10.1\n");
	     if(trigger_veto.count(j)) continue;
	    long offset=args->offset_trigger[tmp.at(0)->type];
	    unsigned long pre=args->pre_trigger[tmp.at(0)->type];;
	    unsigned long post=args->post_trigger[tmp.at(0)->type];;
	    if(it->second->unmerged_triggers.at(j).time + args->offset_trigger[it->second->unmerged_triggers.at(j).type] > tmp.at(0)->time + offset - pre 
	       &&
	       it->second->unmerged_triggers.at(j).time + args->offset_trigger[it->second->unmerged_triggers.at(j).type] < tmp.at(0)->time + offset + post){
	      tmp.push_back(&(it->second->unmerged_triggers.at(j)));
	      trigger_veto[j]=true;
	    }	  
	    
	  }
	  
	  merged_triggers.push_back(tmp);
	}
      }
      
 ////////////////////////////////////////////////////////////////////////
  //printf("d10.9\n");
      
            

      /*	      
	      for(unsigned int i=0; i<it->second->unmerged_triggers.size(); i++){
	      if(!trigger_veto.count(i)){	
	      std::vector< unsigned int> tmp;
	      tmp.push_back(i);
	      
	      for(unsigned int j=i; j<it->second->unmerged_triggers.size(); j++){
	      
	      if(it->second->unmerged_triggers.at(j).time > ( it->second->unmerged_triggers.at(i).time + args->offset_trigger[it->second->unmerged_triggers.at(i).type] - args->pre_trigger[it->second->unmerged_triggers.at(i).type] ) && it->second->unmerged_triggers.at(j).time < ( it->second->unmerged_triggers.at(i).time + args->offset_trigger[it->second->unmerged_triggers.at(i).type] + args->post_trigger[it->second->unmerged_triggers.at(i).type] ) ){
	      tmp.push_back(j);
	      trigger_veto[j]=true;
	      
	      
	      }     
	      }
	      merged_triggers.push_back(tmp);
	      
	      }
	      }
      */
      
      //////////////////////////////////////////////////
      /*        
      //////////////////////////// collecting data////////////////////////
       //printf("d11\n");
      //printf("triggers size=%u\n", merged_triggers.size());      
      for(unsigned int i=0; i<merged_triggers.size(); i++){
	 //printf("d12\n");

	
	unsigned long max_time = merged_triggers.at(i).at(0)->time + args->offset_trigger[merged_triggers.at(i).at(0)->type] + args->post_trigger[merged_triggers.at(i).at(0)->type];
	unsigned long min_time = merged_triggers.at(i).at(0)->time + args->offset_trigger[merged_triggers.at(i).at(0)->type] - args->pre_trigger[merged_triggers.at(i).at(0)->type];;
	
	ReadoutWindow* tmp_readout = new ReadoutWindow;
	//printf("d12.3 pointer =%p\n", tmp_readout);
	tmp_readout->start_counter=min_time;
	tmp_readout->readout_num=args->data->readout_num;
	args->data->readout_num++;
	//printf("max_time=%u\n",max_time);
	//printf("min_time=%u\n",min_time);

	//	printf("sub triggers size\n%u/\n", merged_triggers.at(i).size());
	for( unsigned int j=0; j<merged_triggers.at(i).size(); j++){
	  tmp_readout->triggers_info.push_back(*(merged_triggers.at(i).at(j)));
	}

	if(tmp_readout->triggers_info.size()==0) printf("Error!!!\n");
	
	for(unsigned int bin_count=0; bin_count<3 ; bin_count++){
	  if(it->first==0 && bin_count==0) bin_count = 1;
	  unsigned int bin=it->first -1 + bin_count;
	  if(orphaned){
	    bin_count = 2;
	    bin=it->first;
	  }

	   //printf("bin_count=%u\n",bin_count);
	  //printf("bin=%u\n",bin); 
	  /*
//////////////////// collecting trigger_info//////////////
for(unsigned int j=0; j<merged_triggers.at(i).size(); j++){  
tmp_readout->triggers_info.push_back(it->second->unmerged_triggers.at(merged_triggers.at(i).at(j)));
if( it->second->unmerged_triggers.at(merged_triggers.at(i).at(j)).time + args->offset_trigger[ it->second->unmerged_triggers.at(merged_triggers.at(i).at(j)).type] - args->pre_trigger[ it->second->unmerged_triggers.at(merged_triggers.at(i).at(j)).type] <min_time) min_time = it->second->unmerged_triggers.at(merged_triggers.at(i).at(j)).time + args->offset_trigger[ it->second->unmerged_triggers.at(merged_triggers.at(i).at(j)).type] + args->pre_trigger[ it->second->unmerged_triggers.at(merged_triggers.at(i).at(j)).type]; 
if( it->second->unmerged_triggers.at(merged_triggers.at(i).at(j)).time + args->offset_trigger[ it->second->unmerged_triggers.at(merged_triggers.at(i).at(j)).type] + args->post_trigger[ it->second->unmerged_triggers.at(merged_triggers.at(i).at(j)).type] > max_time) max_time = it->second->unmerged_triggers.at(merged_triggers.at(i).at(j)).time + args->offset_trigger[ it->second->unmerged_triggers.at(merged_triggers.at(i).at(j)).type] + args->post_trigger[ it->second->unmerged_triggers.at(merged_triggers.at(i).at(j)).type];

}
	  */
	  /////////////////////////////////////////////
	   //printf("d13\n");
	  ///////////////////////// collecting mpmt_hits//////////////////////
      /*
	  std::vector<WCTEMPMTHit>::iterator start_hit;
	  std::vector<WCTEMPMTHit>::iterator stop_hit;
	  bool fill=false;
	  
	  if(args->triggered_data[bin]->mpmt_hits.size()>0){
	    start_hit=args->triggered_data[bin]->mpmt_hits.end();
	    start_hit--;
	    stop_hit=args->triggered_data[bin]->mpmt_hits.begin();


	    for(std::vector<WCTEMPMTHit>::iterator it_hit=args->triggered_data[bin]->mpmt_hits.begin(); it_hit!=args->triggered_data[bin]->mpmt_hits.end(); it_hit++){
	      //printf("hit time min|time|max = %u|%u|%u\n", min_time, it_hit->GetCoarseCounter(), max_time);
	      if(it_hit->GetCoarseCounter() > max_time) break;
	      //printf("d13.1\n");
	      if(it_hit->GetCoarseCounter() >= min_time){
		fill=true;
		//printf("d13.2\n");
		if(it_hit->GetCoarseCounter() < start_hit->GetCoarseCounter()) start_hit= it_hit;
		//printf("d13.3\n");
		if(it_hit->GetCoarseCounter() > stop_hit->GetCoarseCounter()) stop_hit= it_hit;
		//printf("d13.4\n");
	      }
	      //printf("d13.4b\n");
	    }
	    //printf("d13.5\n");
	    //printf("start = %u\n", start_hit->GetCoarseCounter()); 
	    //printf("stop = %u\n", stop_hit->GetCoarseCounter());
	    //printf("begin = %u\n", args->triggered_data[bin]->mpmt_hits.begin()->GetCoarseCounter());
	     std::vector<WCTEMPMTHit>::iterator tmp=args->triggered_data[bin]->mpmt_hits.end();
	     tmp--;
	     //printf("end = %u\n", tmp->GetCoarseCounter());
	     //printf("end2 = %u\n", args->triggered_data[bin]->mpmt_hits.at(args->triggered_data[bin]->mpmt_hits.size()-1).GetCoarseCounter());
	    //	tmp_readout->mpmt_hits.resize(size);
	    //std::memcpy(tmp_readout->mpmt_hits.data(), it->second->mpmt_hits.data(), sizeof(it->second->mpmt_hits.data()));
	    stop_hit++;
	    if(fill) tmp_readout->mpmt_hits.insert(tmp_readout->mpmt_hits.end(), start_hit, stop_hit);
	  }
	  /////////////////////////////////////////////////////////////////////////////
	   //printf("d14\n");
	  ///////////////////////// collecting mpmt_waveforms//////////////////////
	  std::vector<WCTEMPMTWaveform>::iterator start_waveform;
	  std::vector<WCTEMPMTWaveform>::iterator stop_waveform;

	  if(args->triggered_data[bin]->mpmt_waveforms.size()>0){
	    start_waveform=args->triggered_data[bin]->mpmt_waveforms.end();
	    start_waveform--;
	    stop_waveform=args->triggered_data[bin]->mpmt_waveforms.begin(); 
	    fill=false;
	    
	    for(std::vector<WCTEMPMTWaveform>::iterator it_waveform=args->triggered_data[bin]->mpmt_waveforms.begin(); it_waveform!=args->triggered_data[bin]->mpmt_waveforms.end(); it_waveform++){
	      if(it_waveform->header.GetCoarseCounter() > max_time) break;
	      if(it_waveform->header.GetCoarseCounter() >= min_time){
		fill=true;
		if(it_waveform->header.GetCoarseCounter() < start_waveform->header.GetCoarseCounter()) start_waveform= it_waveform;
		if(it_waveform->header.GetCoarseCounter() > stop_waveform->header.GetCoarseCounter()) stop_waveform= it_waveform;
		
	      }
	    }
	    
	    //tmp_readout->mpmt_waveforms.resize(size);
	    //std::memcpy(tmp_readout->mpmt_waveforms.data(), it->second->mpmt_waveforms.data(), sizeof(it->second->mpmt_waveforms.data()));
	    stop_waveform++;
	    if(fill) tmp_readout->mpmt_waveforms.insert(tmp_readout->mpmt_waveforms.end(), start_waveform, stop_waveform);
	  }
	  /////////////////////////////////////////////////////////////////////////////
	  //printf("d15\n");
	  
	  ///////////////////////// collecting extra_hits//////////////////////
	  //std::vector<WCTEMPMTHit>::iterator start_hit;
	  //std::vector<WCTEMPMTHit>::iterator stop_hit;

	  if(args->triggered_data[bin]->extra_hits.size()){
	    start_hit=args->triggered_data[bin]->extra_hits.end();
	    start_hit--;
	    stop_hit=args->triggered_data[bin]->extra_hits.begin();
	    fill=false;
	    
	    for(std::vector<WCTEMPMTHit>::iterator it_hit=args->triggered_data[bin]->extra_hits.begin(); it_hit!=args->triggered_data[bin]->extra_hits.end(); it_hit++){
	      if(it_hit->GetCoarseCounter() > max_time) break;
	      if(it_hit->GetCoarseCounter() >= min_time){
		fill=true;
		if(it_hit->GetCoarseCounter() < start_hit->GetCoarseCounter()) start_hit= it_hit;
		if(it_hit->GetCoarseCounter() > stop_hit->GetCoarseCounter()) stop_hit= it_hit;
	      }
	    }
	    
	    //	tmp_readout->mpmt_hits.resize(size);
	    //std::memcpy(tmp_readout->mpmt_hits.data(), it->second->mpmt_hits.data(), sizeof(it->second->mpmt_hits.data()));
	    stop_hit++;
	    if(fill) tmp_readout->extra_hits.insert(tmp_readout->extra_hits.end(), start_hit, stop_hit);
	  }
	  /////////////////////////////////////////////////////////////////////////////
	   //printf("d16\n");
	  ///////////////////////// collecting extra_waveforms//////////////////////
	  //std::vector<WCTEMPMTWaveform>::iterator start_waveform;
	  //std::vector<WCTEMPMTWaveform>::iterator stop_waveform;

	   if(args->triggered_data[bin]->extra_waveforms.size()>0){
	     start_waveform=args->triggered_data[bin]->extra_waveforms.end();
	     start_waveform--;
	     stop_waveform=args->triggered_data[bin]->extra_waveforms.begin();
	     fill=false;
	     
	  for(std::vector<WCTEMPMTWaveform>::iterator it_waveform=args->triggered_data[bin]->extra_waveforms.begin(); it_waveform!=args->triggered_data[bin]->extra_waveforms.end(); it_waveform++){
	    if(it_waveform->header.GetCoarseCounter() > max_time) break;
	    if(it_waveform->header.GetCoarseCounter() >= min_time){
	      fill=true;
	      if(it_waveform->header.GetCoarseCounter() < start_waveform->header.GetCoarseCounter()) start_waveform= it_waveform;
	      if(it_waveform->header.GetCoarseCounter() > stop_waveform->header.GetCoarseCounter()) stop_waveform= it_waveform;
	      
	    }
	  }
	  
	  //tmp_readout->mpmt_waveforms.resize(size);
	  //std::memcpy(tmp_readout->mpmt_waveforms.data(), it->second->mpmt_waveforms.data(), sizeof(it->second->mpmt_waveforms.data()));
	  stop_waveform++;
	  if(fill) tmp_readout->extra_waveforms.insert(tmp_readout->extra_waveforms.end(), start_waveform, stop_waveform);
	   }
	   /////////////////////////////////////////////////////////////////////////////
	   
	}
      
	//printf("d17\n");
	
	//////////////////////  collecting qdc and tdc hits  /////////////////////////	  
	if(merged_triggers.at(i).at(0)->type==TriggerType::MAIN){
	  
	  
	  //  unsigned long vme_event_vec_num=merged_triggers.at(i).at(0)).vme_event_num - args->data->vme_poped;
	  tmp_readout->tdc_hits = args->data->tdc_readout.getEvent();
	  tmp_readout->qdc_hits = args->data->qdc_readout.getEvent();
	}	     
	
	/// ///////////// finished collecting data /////////////////////////////
	
	
	*/
	
	//////////////////////////////////////////////////////////////////////////
	 //printf("d18\n");
	//adding readout window
	 //printf("d18.1 pointer =%p\n", tmp_readout);
      //	args->data->readout_windows_mtx.lock();
	 //printf("d18.2\n");
	 //printf("d18.3 size=%d\n", args->data->readout_windows->size());
	//tmp_readout->Print();
	//	args->data->readout_windows->push_back(tmp_readout);
	 //printf("d18.4\n");
      //args->data->readout_windows_mtx.unlock();
	//delete tmp_readout;
	//printf("d19\n");
  /*	
      }
      if(orphaned) erase_list.push_back(it->first);
      else if(it->first !=0) erase_list.push_back(it->first - 1); ///BEN!!!! THIS NEEDS CHECKING
      //printf("d20\n");
  }
    
  //}
  
  //printf("d21\n");
  /// deleting data;
  */
  for (std::vector<unsigned int>::iterator it=erase_list.begin(); it!=erase_list.end(); it++){
    //printf("d22\n");
    //    delete args->triggered_data[*it];
    args->triggered_data[*it]=0;

    args->triggered_data.erase(*it);

  }
  //printf("d23\n");
  erase_list.clear();
  printf("d24\n");
  shared_ptrs.clear();
  printf("d24\n"); 

}

void WindowBuilder3::LoadConfig(){

    if(!m_variables.Get("verbose",m_verbose)) m_verbose=1;

    args->pre_trigger.clear();
    args->post_trigger.clear();
    args->offset_trigger.clear();
    /*    
    bool beamelec=false;
    unsigned long beamelec_pre_trigger=0;
    unsigned long beamelec_post_trigger=0;
    long beamelec_offset_trigger=0;
    m_variables.Get("beamelec", beamelec);
    m_variables.Get("beamelec_pre_trigger", beamelec_pre_trigger);
    m_variables.Get("beamelec_post_trigger", beamelec_post_trigger);
    m_variables.Get("beamelec_offset_trigger", beamelec_offset_trigger);
    if(beamelec){
      args->pre_trigger[TriggerType::EBEAM]=beamelec_pre_trigger;
      args->post_trigger[TriggerType::EBEAM]=beamelec_post_trigger;
      args->offset_trigger[TriggerType::EBEAM]=beamelec_offset_trigger;
    }

    
    bool beammu=false;
    unsigned long beammu_pre_trigger=0;
    unsigned long beammu_post_trigger=0;
    long beammu_offset_trigger=0;
    m_variables.Get("beamelec", beammu);
    m_variables.Get("beammu_pre_trigger", beammu_pre_trigger);
    m_variables.Get("beammu_post_trigger", beammu_post_trigger);
    m_variables.Get("beammu_offset_trigger", beammu_offset_trigger);
    if(beammu){
      args->pre_trigger[TriggerType::MBEAM]=beammu_pre_trigger;
      args->post_trigger[TriggerType::MBEAM]=beammu_post_trigger;
      args->offset_trigger[TriggerType::MBEAM]=beammu_offset_trigger;
    }
    */
    
    bool led=false;
    unsigned long led_pre_trigger=0;
    unsigned long led_post_trigger=0;
    long led_offset_trigger=0;
    m_variables.Get("led", led);
    m_variables.Get("led_pre_trigger", led_pre_trigger);
    m_variables.Get("led_post_trigger", led_post_trigger);
    m_variables.Get("led_offset_trigger", led_offset_trigger);
    if(led){
      args->pre_trigger[TriggerType::LED]=led_pre_trigger;
      args->post_trigger[TriggerType::LED]=led_post_trigger;
      args->offset_trigger[TriggerType::LED]=led_offset_trigger;
    }
    /*
      bool laser=false;
      unsigned long laser_pre_trigger=0;
      unsigned long laser_post_trigger=0;
      long laser_offset_trigger=0;
      m_variables.Get("laser", laser);
      m_variables.Get("laser_pre_trigger", laser_pre_trigger);
      m_variables.Get("laser_post_trigger", laser_post_trigger);
      m_variables.Get("laser_offset_trigger", laser_offset_trigger);
      if(laser){
      args->pre_trigger[TriggerType::LASER]=laser_pre_trigger;
      args->post_trigger[TriggerType::LASER]=laser_post_trigger;
      args->offset_trigger[TriggerType::LASER]=laser_offset_trigger;
      }
      
      bool none=false;
      unsigned long none_pre_trigger=0;
      unsigned long none_post_trigger=0;
      long none_offset_trigger=0;
      m_variables.Get("none", none);
      m_variables.Get("none_pre_trigger", none_pre_trigger);
      m_variables.Get("none_post_trigger", none_post_trigger);
      m_variables.Get("none_offset_trigger", none_offset_trigger);
      if(none){
      args->pre_trigger[TriggerType::NONE]=none_pre_trigger;
      args->post_trigger[TriggerType::NONE]=none_post_trigger;
      args->offset_trigger[TriggerType::NONE]=none_offset_trigger;
      }
    */
      
    bool nhits=false;
    unsigned long nhits_pre_trigger=0;
    unsigned long nhits_post_trigger=0;
    long nhits_offset_trigger=0;
    m_variables.Get("nhits", nhits);
    m_variables.Get("nhits_pre_trigger", nhits_pre_trigger);
    m_variables.Get("nhits_post_trigger", nhits_post_trigger);
    m_variables.Get("nhits_offset_trigger", nhits_offset_trigger);
    if(nhits){
      args->pre_trigger[TriggerType::NHITS]=nhits_pre_trigger;
      args->post_trigger[TriggerType::NHITS]=nhits_post_trigger;
      args->offset_trigger[TriggerType::NHITS]=nhits_offset_trigger;
    }
    /*
      bool hard6=false;
      unsigned long hard6_pre_trigger=0;
      unsigned long hard6_post_trigger=0;
      long hard6_offset_trigger=0;
      m_variables.Get("hard6", hard6);
      m_variables.Get("hard6_pre_trigger", hard6_pre_trigger);
      m_variables.Get("hard6_post_trigger", hard6_post_trigger);
      m_variables.Get("hard6_offset_trigger", hard6_offset_trigger);
      if(hard6){
      args->pre_trigger[TriggerType::HARD6]=hard6_pre_trigger;
      args->post_trigger[TriggerType::HARD6]=hard6_post_trigger;
      args->offset_trigger[TriggerType::HARD6]=hard6_offset_trigger;
      }
    */
    
    bool main=false;
    unsigned long main_pre_trigger=0;
    unsigned long main_post_trigger=0;
    long main_offset_trigger=0;
    m_variables.Get("main", main);
    m_variables.Get("main_pre_trigger", main_pre_trigger);
    m_variables.Get("main_post_trigger", main_post_trigger);
    m_variables.Get("main_offset_trigger", main_offset_trigger);
    if(main){
      args->pre_trigger[TriggerType::MAIN]=main_pre_trigger;
      args->post_trigger[TriggerType::MAIN]=main_post_trigger;
      args->offset_trigger[TriggerType::MAIN]=main_offset_trigger;
    }
    
}

bool WindowBuilder3::BuildWindow(void* data){

 BuildWindow_args* args=reinterpret_cast<BuildWindow_args*>(data);
 
 // for (int i=0; i<args->triggered_data.size(); i++){
 printf("u1 Pre,post,size %d,%d,%lu\n",args->pre_cluster,args->post_cluster,args->triggered_data.size());

 /*if(args->pre_cluster){
 printf("u1.1\n");
    delete args->triggered_data.at(0);
 printf("u1.2\n");
   args->triggered_data.at(0)=0;
 printf("u1.3\n");
 }
  printf("u1.5\n");
 if(!args->post_cluster){
 printf("u1.6\n");
   delete args->triggered_data.at(args->triggered_data.size()-1);
 printf("u1.7\n");
   args->triggered_data.at(args->triggered_data.size()-1)=0;
 printf("u1.8\n");
 }
 */
 printf("u2\n");
 args->triggered_data.clear();
  printf("u2.1\n");
 /* 
 for (int i=0; i<args->merged_triggers.size(); i++){
   for (int j=0; j<args->merged_triggers.at(i).size(); j++){
 printf("u3\n");
     delete args->merged_triggers.at(i).at(j);
 printf("u4\n");
   }
   args->merged_triggers.at(i).clear();
 }
 args->merged_triggers.clear();
 */
 args->data=0;
 printf("u5\n");
 // delete args;
  printf("u6\n");
 args=0;
 
 return true;

 
}
