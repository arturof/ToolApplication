#include "MPMT3.h"

MPMT3Messages::MPMT3Messages(){
  //  daq_header=0;
  //mpmt_data=0;
  mpmt_message=0;
  m_data=0;
  time_corrections=0;
}

MPMT3Messages::~MPMT3Messages(){
  // delete daq_header;
  //daq_header=0;
  //delete mpmt_data;
  // mpmt_data=0;
  mpmt_message=0;
  m_data=0;
  time_corrections=0; 
}

MPMT3_args::MPMT3_args():Thread_args(){
  data_sock=0;
  utils=0;
  job_queue=0;
  m_data=0;

}

MPMT3_args::~MPMT3_args(){
  delete data_sock;
  data_sock=0;
  delete utils;
  utils=0;

  for(std::map<std::string,Store*>::iterator it=connections.begin(); it!= connections.end(); it++){
    delete it->second;
    it->second=0;
  }

  connections.clear();
  job_queue=0;
  m_data=0;
  
}


MPMT3::MPMT3():Tool(){}


bool MPMT3::Initialise(std::string configfile, DataModel &data){

  InitialiseTool(data);
  InitialiseConfiguration(configfile);
  //m_variables.Print();
  
  if(!m_variables.Get("verbose",m_verbose)) m_verbose=1;
  if(!m_variables.Get("MPMT_port",m_mpmt_port)) m_mpmt_port="4444";
  if(!m_variables.Get("MPMT_search_period_sec",m_mpmt_search_period_sec)) m_mpmt_search_period_sec=10;
 
  m_util=new Utilities();

  //laod port number maybe
  //load rounding of bins masybe
  
  m_threadnum=0;
  CreateThread();
  m_freethreads=1;
  
  ExportConfiguration();

  
  
  
 return true;
}


bool MPMT3::Execute(){

  for(std::map<std::string, unsigned int>::iterator it=m_data->hit_map.begin(); it!=m_data->hit_map.end(); it++){
    m_data->monitoring_store_mtx.lock();
    m_data->monitoring_store.Set(it->first, it->second);
    m_data->monitoring_store_mtx.unlock();
  }
  /*
  usleep (1000);
  m_data->unsorted_data_mtx.lock();
  m_data->unsorted_data.clear();
  m_data->unsorted_data_mtx.unlock();
  */
  /*
  for(unsigned int i=0; i<args.size(); i++){
    if(args.at(i)->busy==0){
      *m_log<<"reply="<<args.at(i)->message<<std::endl;
      args.at(i)->message="Hi";
      args.at(i)->busy=1;
      break;
    }

  }

  m_freethreads=0;
  unsigned int lastfree=0;
  for(unsigned int i=0; i<args.size(); i++){
    if(args.at(i)->busy==0){
      m_freethreads++;
      lastfree=i; 
    }
  }

  if(m_freethreads<1) CreateThread();
  if(m_freethreads>1) DeleteThread(lastfree);
  
  *m_log<<ML(1)<<"free threads="<<m_freethreads<<":"<<args.size()<<std::endl;
  MLC();
  
  // sleep(1);  for single tool testing
  */
  return true;
}


bool MPMT3::Finalise(){

  for(unsigned int i=0;i<args.size();i++) DeleteThread(0);
  
  args.clear();
  
  delete m_util;
  m_util=0;

  return true;
}

void MPMT3::CreateThread(){

  MPMT3_args* tmparg=new MPMT3_args();
 
  tmparg->data_sock=new zmq::socket_t(*(m_data->context), ZMQ_ROUTER);
  tmparg->data_port=m_mpmt_port;
  tmparg->utils= new DAQUtilities(m_data->context);
  
  tmparg->items[0].socket=*tmparg->data_sock;
  tmparg->items[0].fd=0;
  tmparg->items[0].events=ZMQ_POLLIN;
  tmparg->items[0].revents=0;
  
  tmparg->period=boost::posix_time::seconds(m_mpmt_search_period_sec);
  
  tmparg->last=boost::posix_time::microsec_clock::universal_time();
  tmparg->m_data=m_data;
  tmparg->job_queue=&(m_data->job_queue);
  tmparg->hit_rates=&m_data->hit_map;

  std::string config_json="";
  m_data->services->GetDeviceConfig(config_json, -1, "MCCAssignments");
  Store times;
  times.JsonParser(config_json);
  
  int correction =0;
  for(unsigned int i=0; i<200; i++){
    if(times.Get(std::to_string(i),correction)){
      tmparg->time_corrections[i]=correction;
    }

  }
  
  
  args.push_back(tmparg);
  std::stringstream tmp;
  tmp<<"T"<<m_threadnum;
  m_util->CreateThread(tmp.str(), &Thread, args.at(args.size()-1));
  m_threadnum++;
  m_data->thread_num++;

}

 void MPMT3::DeleteThread(unsigned int pos){

   m_util->KillThread(args.at(pos));

   delete args.at(pos)->data_sock;
   args.at(pos)->data_sock=0;

   delete args.at(pos)->utils;
   args.at(pos)->utils=0;
   
   delete args.at(pos);
   args.at(pos)=0;
   args.erase(args.begin()+(pos));
   

 }

void MPMT3::Thread(Thread_args* arg){

  MPMT3_args* args=reinterpret_cast<MPMT3_args*>(arg);

  args->lapse = args->period -( boost::posix_time::microsec_clock::universal_time() - args->last);
  
  if( args->lapse.is_negative()){
    //printf("in lapse \n");
    unsigned short num_connections = args->connections.size();
    if(args->utils->UpdateConnections("TPMT", args->data_sock, args->connections, args->data_port) > num_connections) args->m_data->services->SendLog("Info: New MPMT connected",v_message); //add pmt id
    if(args->utils->UpdateConnections("MPMT", args->data_sock, args->connections, args->data_port) > num_connections) args->m_data->services->SendLog("Info: New MPMT connected",v_message); //add pmt id
    args->m_data->monitoring_store_mtx.lock();
    args->m_data->monitoring_store.Set("connected_MPMTs",num_connections);
    args->m_data->monitoring_store_mtx.unlock();
    args->last= boost::posix_time::microsec_clock::universal_time();
    printf("conenctions=%ld: %lu\n",args->connections.size(), args->m_data->unsorted_data.size());
  }
  
  zmq::poll(&(args->items[0]), 1, 100);
  
  if(args->items[0].revents & ZMQ_POLLIN){
    //printf("received data\n");
    
    zmq::message_t identity;

    //zmq::message_t* daq_header = new zmq::message_t;
    //zmq::message_t* mpmt_data = new zmq::message_t;

    args->message_received=false;
    args->no_data=false;
    
    args->message_received=args->data_sock->recv(&identity);     

    if(!identity.more() || !args->message_received || identity.size() == 0){
      args->m_data->services->SendLog("Warning: MPMT thread identity has no size or only message",3);
      //   delete mpmt_data;
      //mpmt_data=0;
      //delete daq_header;
      //daq_header=0;
      return;
    }

    MPMTMessage* mpmt_message = new MPMTMessage;
    
    args->message_received=args->data_sock->recv(&mpmt_message->daq_header);
    
    if(!args->message_received || mpmt_message->daq_header.size()!=DAQHeader::GetSize() ){
      args->m_data->services->SendLog("Warning: MPMT thread daq header has no or incorrect size",3);
      delete mpmt_message;
      mpmt_message=0;
      // delete daq_header;
      // daq_header=0;
      return;
    }
    if(!mpmt_message->daq_header.more()) args->no_data=true;
    else{
      args->message_received=args->data_sock->recv(&mpmt_message->mpmt_data);
      if(mpmt_message->mpmt_data.more() || !args->message_received || mpmt_message->mpmt_data.size() == 0){
	args->m_data->services->SendLog("ERROR: MPMT thread too many message parts or no data, throwing away data",2); //add mpmtid
	zmq::message_t throwaway;
	if(mpmt_message->mpmt_data.more()){
	  args->data_sock->recv(&throwaway);
	  while(throwaway.more()) args->data_sock->recv(&throwaway);
	}
	delete mpmt_message;
	mpmt_message=0;
	//delete daq_header;
	//daq_header=0;
	return;
      }
    }
    //printf("data fine\n");
	
    zmq::message_t reply(4);
    //printf("d1\n");
    memcpy(reply.data(), mpmt_message->daq_header.data(), reply.size());
    //printf("sending reply\n");
    args->data_sock->send(identity, ZMQ_SNDMORE); /// need to add checking probablly a poll incase sender dies
    args->data_sock->send(reply);
    //printf("sent reply\n");
    
    if(args->no_data){
      delete mpmt_message;
      mpmt_message=0;
      //      delete daq_header;
      //daq_header=0;
    }
    else{
      //printf("creating job\n");
      Job* tmp_job= new Job("MPMT");
      MPMT3Messages* tmp_msgs= new MPMT3Messages;
      tmp_msgs->time_corrections=&(args->time_corrections[0]);
      // tmp_msgs->daq_header=daq_header;
      //  tmp_msgs->mpmt_data=mpmt_data;
      tmp_msgs->mpmt_message=mpmt_message;
      tmp_msgs->m_data=args->m_data;
      tmp_job->data= tmp_msgs;
      tmp_msgs->hit_rates=args->hit_rates;
      //printf("d1\n");
      tmp_job->func=ProcessData;
      //printf("d2\n");
      //delete tmp_msgs;
      //delete mpmt_message;
      //delete tmp_job;
      //printf("d3\n");
      args->job_queue->AddJob(tmp_job);
      //sleep(5);
      //printf("job submitted %d\n",args->job_queue->size());
    }
  }
  
} //job deletion needed



bool MPMT3::ProcessData(void* data){

  //printf("in process data\n");
  MPMT3Messages* msgs=reinterpret_cast<MPMT3Messages*>(data);
  //printf("d1\n");  
  DAQHeader* daq_header=reinterpret_cast<DAQHeader*>(msgs->mpmt_message->daq_header.data());

  unsigned int bin= daq_header->GetCoarseCounter();  

  unsigned short card_id = daq_header->GetCardID();
  unsigned short card_type = daq_header->GetCardType();

  //printf("%u:%u\n", daq_header->GetCardID(), daq_header->GetCoarseCounter());
  
  std::vector<MPMTHit*> triggers;
  std::vector<MPMTLED*> leds;
  std::vector<TriggerInfo*> triggers_info;
  
  //    if(card_type==3U){
  // card_type=3U;
    //find triggers

  
  unsigned long bytes=msgs->mpmt_message->mpmt_data.size();
  unsigned long current_byte=0;
    //printf("%u:%u\n", daq_header->GetCardID(), daq_header->GetCoarseCounter());
    /*if((msgs->m_data->current_coarse_counter >>22) >2000){  
      if(bin< ((msgs->m_data->current_coarse_counter >>22) - 2000) || bin > ((msgs->m_data->current_coarse_counter >>22) + 2000)){ //~2 seconds seconds away from current time (60 x ~33.5ms)
	
	std::string tmp="ERROR: Data from MPMT" +  std::to_string(card_id)  +" is out of temporal threshold, maybe desynced (got:" + std::to_string(bin) +" expected: ~" + std::to_string(msgs->m_data->current_coarse_counter >>22);
	msgs->m_data->services->SendLog(tmp,0);
	delete msgs->daq_header;
	msgs->daq_header=0;
	delete msgs->mpmt_data;
	msgs->mpmt_data=0;
	delete msgs;
	msgs=0;
	return false;                 
	
      }
    }
    */
  //printf("d2 car_id=%d\n", card_id);
  //  daq_header->Print();
  //  std::map<unsigned int, MPMTData*> local_data;
  /*  std::vector<WCTEMPMTHit> vec_mpmt_hit;
  std::vector<WCTEMPMTLED> vec_mpmt_led;
  std::vector<WCTEMPMTPPS> vec_mpmt_pps;
  std::vector<WCTEMPMTWaveform> vec_mpmt_waveform;
  std::vector<WCTEMPMTHit> vec_triggers_hit;
  */
 //printf("d3\n");
  std::string pmt_unique_name="";
  unsigned char* mpmt_data= reinterpret_cast<unsigned char*>(msgs->mpmt_message->mpmt_data.data());
  ////printf("data size %d\n",msgs->mpmt_data->size());
   //printf("d4\n");
  while(current_byte<bytes && (bytes-current_byte)>8){
    
    //printf("d5 curent:total= %d:%d\n", current_byte, bytes);
    //printf("cuurent byte %d : %d\n",mpmt_data[current_byte], (mpmt_data[current_byte] >> 6));
    //printf("(mpmt_data[current_byte] >> 6) == 0b1:%d\n", ((mpmt_data[current_byte] >> 6) == 0b1));
    //printf("testing current byte\n");
    //printf("current byte=%u\n",(mpmt_data[current_byte] >> 6));
    if((mpmt_data[current_byte] >> 6) == 0b1){ //its a hit or led or pps
      //printf("in hit, led or pps \n");
      if(((mpmt_data[current_byte] >> 2) & 0b00001111 ) == 0U && bytes-current_byte >= WCTEMPMTHit::GetSize()){ // its normal mpmt hit
	//printf("in hit\n");
	MPMTHit* tmp= reinterpret_cast<MPMTHit*>(&mpmt_data[current_byte]);
	//	  tmp(card_id, &mpmt_data[current_byte]);
	//tmp.SetCoarseCounter(tmp.GetCoarseCounter() + 12*msgs->time_corrections[card_id]);
	//	UWCTEMPMTHit* bob = reinterpret_cast<UWCTEMPMTHit*>(&tmp);
	if(tmp->GetChannel()<10 && card_type==3U){
	  //triggers.push_back(tmp);
	  TriggerInfo* trigger_info = new TriggerInfo;
	  switch(tmp->GetChannel()){
	    
	  case 0U: //main trigger
	     trigger_info->type = TriggerType::MAIN;
	    break;
	    
	  case 1U: //beam monitor - electron
	     trigger_info->type = TriggerType::EBEAM;
	    break;
	    
	  case 2U: //beam monitor - muon
	     trigger_info->type = TriggerType::MBEAM;
	    break;
	    
	  case 5U: //CDS laser
	     trigger_info->type = TriggerType::LASER;
	    break;
	    
	  case 7U:
	     trigger_info->type = TriggerType::HARD6;
	    break;

	  default:
	     trigger_info->type = TriggerType::NONE;
	    break;
	  }

	  trigger_info->time= ((daq_header->GetCoarseCounter() & 4294901760U )<<16)  | (tmp->GetCoarseCounter());
	  if((daq_header->GetCoarseCounter() & 65535U) > (tmp->GetCoarseCounter() >>16)) trigger_info->time+=65536U;
	  trigger_info->card_id=card_id;
	  triggers_info.push_back(trigger_info);
	}
	
	current_byte+=WCTEMPMTHit::GetSize();
	pmt_unique_name=std::to_string(card_id)+":"+std::to_string(tmp->GetChannel());
	if(!msgs->hit_rates->count(pmt_unique_name)) (*msgs->hit_rates)[pmt_unique_name]=0;
	(*msgs->hit_rates)[pmt_unique_name]++;
	
	/*	unsigned int tmp_bin = (daq_header->GetCoarseCounter() & 4294901760U ) | (tmp.GetCoarseCounter() >>16);
	if((daq_header->GetCoarseCounter() & 65535U) > (tmp.GetCoarseCounter() >>16)){
	   //printf("this should not print alot\n");
	   tmp_bin +=65536U;
	}
	tmp_bin = tmp_bin >> 6;
	if(!local_data.count(tmp_bin)){
	  local_data[tmp_bin] = new MPMTData();
	  local_data[tmp_bin]->coarse_counter=tmp_bin<<6;
	}
	//printf("cardtype=%u\n",card_type);
	// printf("cardchannel=%u\n",tmp.GetChannel());
	if(card_type==3U){
	  if(tmp.GetChannel() < 10){
	    //printf("its a trigger bin=%u\n",tmp_bin);
	    local_data[tmp_bin]->mpmt_triggers.push_back(tmp);
	  }
	  //else {
	    local_data[tmp_bin]->extra_hits.push_back(tmp);
	    //printf("its an extra trigger=hit\n");
	    //}
	}
	else{
	  local_data[tmp_bin]->mpmt_hits.push_back(tmp);
	  //printf("its a normal hit\n");
	}
	*/
      }
	
      //else if(((mpmt_data[current_byte] >> 2) & 0b00001111 ) == 1U ){
	//printf("in ped \n");
      //      }// its a pedistal (dont know) 
      
      else if(((mpmt_data[current_byte] >> 2) & 0b00001111 ) == 2U && bytes-current_byte >= WCTEMPMTLED::GetSize()){// its LED
	printf("in led \n");
	//	WCTEMPMTLED tmp(card_id, &mpmt_data[current_byte]);
	//tmp.SetCoarseCounter(tmp.GetCoarseCounter() + 12*msgs->time_corrections[card_id]);

	MPMTLED* tmp = reinterpret_cast<MPMTLED*>(&mpmt_data[current_byte]);

	leds.push_back(tmp);

	TriggerInfo* trigger_info = new TriggerInfo;
	trigger_info->type= TriggerType::LED;
	trigger_info->time= ((daq_header->GetCoarseCounter() & 4294901760U )<<16)  | (tmp->GetCoarseCounter());
	if((daq_header->GetCoarseCounter() & 65535U) > (tmp->GetCoarseCounter() >>16)) trigger_info->time+=65536U;
	trigger_info->card_id=card_id;
	triggers_info.push_back(trigger_info);
	
	current_byte+=WCTEMPMTLED::GetSize();
	//unsigned int tmp_bin = (daq_header->GetCoarseCounter() & 4294901760U ) | (tmp.GetCoarseCounter() >>16);
        //if((daq_header->GetCoarseCounter() & 65535U) > (tmp.GetCoarseCounter() >>16)){
	  //printf("this should not print alot\n");
	  //tmp_bin +=65536U;
	//}
	//tmp_bin = tmp_bin >> 6;
	//if(!local_data.count(tmp_bin)){
	// local_data[tmp_bin] = new MPMTData();
	// local_data[tmp_bin]->coarse_counter=tmp_bin<<6;
	//}
	// local_data[tmp_bin]->mpmt_leds.push_back(tmp);
      }
      
      //      else if(((mpmt_data[current_byte] >> 2) & 0b00001111 ) == 3U){
	//printf("in calib \n");
      //	}// its calib??
      
      else if(((mpmt_data[current_byte] >> 2) & 0b00001111 ) == 15U && bytes-current_byte >= WCTEMPMTPPS::GetSize() ){// its PPS
	//printf("in pps\n");
	current_byte+=WCTEMPMTPPS::GetSize();
	/*	WCTEMPMTPPS tmp(card_id, &mpmt_data[current_byte]);
	current_byte+=WCTEMPMTPPS::GetSize();
	unsigned int tmp_bin = (daq_header->GetCoarseCounter() & 4294901760U ) | (tmp.GetCoarseCounter() >>16);
        if((daq_header->GetCoarseCounter() & 65535U) > (tmp.GetCoarseCounter() >>16)) tmp_bin +=65536U;
        tmp_bin = tmp_bin >> 6;
        local_data[tmp_bin].mpmt_pps.push_back(tmp);
	*/
      }
      else{
	 //printf("none of hit LED or pps despite saying it is\n");
	std::string tmp="ERROR: Data from MPMT" +  std::to_string(card_id)  +" is courupt or of uknown structure";
	//	for(std::map<unsigned int, MPMTData*>::iterator it=local_data.begin(); it!=local_data.end(); it++){
	//	  delete it->second;
	//it->second=0;
	//}
	//local_data.clear();
	 msgs->m_data->services->SendLog(tmp,0);
	 // delete msgs->daq_header;
	 //msgs->daq_header=0;
	 //delete msgs->mpmt_data;
	 //msgs->mpmt_data=0;
	 delete msgs->mpmt_message;
	 msgs->mpmt_message=0;
	 for(unsigned int i=0; i<triggers_info.size(); i++){
	   delete triggers_info.at(i);
	   triggers_info.at(i)=0;
	 }
	 delete msgs;
	 msgs=0;
	return false;

      }
    }
    else if ((mpmt_data[current_byte] >> 6) == 2U && bytes-current_byte >= WCTEMPMTWaveformHeader::GetSize()){ //its a waveform
      //printf("in Waveform\n");
       //printf("k0\n");
      //WCTEMPMTWaveform tmp(card_id, &mpmt_data[current_byte]);
      //tmp.header.SetCoarseCounter(tmp.header.GetCoarseCounter() + 12*msgs->time_corrections[card_id]);
      // printf("k1 channel=%u\n", tmp.header.GetChannel());
      MPMTWaveformHeader* tmp = reinterpret_cast<MPMTWaveformHeader*>(&mpmt_data[current_byte]);
      current_byte+=  WCTEMPMTWaveformHeader::GetSize() + tmp->GetLength();
      //printf("k2\n");
       /*
       if(bytes-current_byte >= tmp.header.GetLength()){
	 printf("k3\n");
	 tmp.samples.resize(tmp.header.GetLength());
	 printf("k4\n");
	 memcpy(tmp.samples.data(), &mpmt_data[current_byte], tmp.header.GetLength());
	 printf("k5\n");
	 current_byte+=(tmp.header.GetLength());
	 printf("k6\n");
	 UWCTEMPMTWaveformHeader* tmp2 = reinterpret_cast<UWCTEMPMTWaveformHeader*>(&(tmp.header));
	 std::stringstream hello;
	 hello<<tmp2->bits;
	 printf("k6.5 %s\n",hello.str().c_str());
	 vec_mpmt_waveform.push_back(tmp);
	 printf("k7\n");
       }
       */
       //	unsigned int tmp_bin = (daq_header->GetCoarseCounter() & 4294901760U ) | (tmp.header.GetCoarseCounter() >>16);
       //if((daq_header->GetCoarseCounter() & 65535U) > (tmp.header.GetCoarseCounter() >>16)) tmp_bin +=65536U;
       //	tmp_bin = tmp_bin >> 6;
	//printf("k1 channel=%u : %u\n", tmp.header.GetChannel(), tmp_bin);

	//if(!local_data.count(tmp_bin)){
       // local_data[tmp_bin] = new MPMTData();
       //  local_data[tmp_bin]->coarse_counter=tmp_bin<<6;
       //}
       //  if(card_type!=3U) local_data[tmp_bin]->mpmt_waveforms.push_back(tmp);
	  //	if(card_type==3U && tmp.header.GetChannel() >= 10) local_data[tmp_bin]->extra_waveforms.push_back(tmp);
       //if(card_type==3U) local_data[tmp_bin]->extra_waveforms.push_back(tmp);
       }
    else{
      //printf("currentbyte=%u\n",current_byte);
      //printf("bytes=%u\n",bytes);
      //printf("(mpmt_data[current_byte] >> 6)%u\n", (mpmt_data[current_byte] >> 6));
      //printf("(mpmt_data[current_byte] >> 6)%u\n", (mpmt_data[current_byte] >> 6));
      
      std::string tmp="ERROR: Waveform data from MPMT" +  std::to_string(card_id)  +" is courupt or of uknown structure.";
      msgs->m_data->services->SendLog(tmp,0);
      // for(std::map<unsigned int, MPMTData*>::iterator it=local_data.begin(); it!=local_data.end(); it++){
      //	delete it->second;
      //	it->second=0;
      //}
      //local_data.clear();
      // delete msgs->daq_header;
      //msgs->daq_header=0;
      //delete msgs->mpmt_data;
      //msgs->mpmt_data=0;
      delete msgs->mpmt_message;
      msgs->mpmt_message=0;
      for(unsigned int i=0; i<triggers_info.size(); i++){
	delete triggers_info.at(i);
	triggers_info.at(i)=0;
      }
      delete msgs;
      msgs=0;
      return false;
      
    }
    }
  //  }

  //printf("data processed \n");
  
  /////////////////////////////////////////////////
  ////adding data to datamodel
  ////////////////////////////////////////////////////
  //printf("d5\n");

    msgs->m_data->data_chunks_mtx.lock();
    if(msgs->m_data->data_chunks.count(bin)==0){
      msgs->m_data->data_chunks[bin]=new MPMTCollection;
    }

    msgs->m_data->data_chunks[bin]->mtx.lock();
    msgs->m_data->data_chunks[bin]->mpmt_output.push_back(msgs->mpmt_message);
    /*    if(triggers.size())   msgs->m_data->data_chunks[bin]->triggers.insert( msgs->m_data->data_chunks[bin]->triggers.end(), triggers.begin(), triggers.end());
    if(leds.size()) msgs->m_data->data_chunks[bin]->leds.insert( msgs->m_data->data_chunks[bin]->leds.end(), leds.begin(),leds.end());
    */    
if(triggers_info.size())   msgs->m_data->data_chunks[bin]->triggers_info.insert( msgs->m_data->data_chunks[bin]->triggers_info.end(), triggers_info.begin(), triggers_info.end());

    
    msgs->m_data->data_chunks[bin]->mtx.unlock();

    msgs->m_data->data_chunks_mtx.unlock();
	
  /*
  msgs->m_data->unsorted_data_mtx.lock();
  for(std::map<unsigned int, MPMTData*>::iterator it=local_data.begin(); it!=local_data.end(); it++){
    delete it->second;
    it->second=0;
    
    //printf("d6\n");
    //printf("time %u:MPMT%u:current=%u\n", it->first, card_id, ((msgs->m_data->current_coarse_counter >>22) - 60));
    //    it->second.Print();
    //   msgs->m_data->unsorted_data_mtx.lock();
    if(msgs->m_data->unsorted_data.count(it->first)==0){
      msgs->m_data->unsorted_data[it->first]=it->second;
      it->second=0;
      
           
      delete msgs->m_data->unsorted_data[it->first];
      msgs->m_data->unsorted_data[it->first]=0;
      msgs->m_data->unsorted_data.erase(it->first);
      
      continue;
      //      msgs->m_data->unsorted_data[it->first]=new MPMTData();
      // msgs->m_data->unsorted_data[it->first]->coarse_counter=(it->first<<6);
    }
    //msgs->m_data->unsorted_data_mtx.unlock();
    
    if(it->second->mpmt_hits.size() > 0){
      msgs->m_data->unsorted_data[it->first]->mpmt_hits_mtx.lock();
      msgs->m_data->unsorted_data[it->first]->mpmt_hits.insert( msgs->m_data->unsorted_data[it->first]->mpmt_hits.end(), it->second->mpmt_hits.begin(), it->second->mpmt_hits.end());
       msgs->m_data->unsorted_data[it->first]->mpmt_hits_mtx.unlock();
    }
    if(it->second->mpmt_leds.size() > 0){
      msgs->m_data->unsorted_data[it->first]->mpmt_leds_mtx.lock();
      msgs->m_data->unsorted_data[it->first]->mpmt_leds.insert( msgs->m_data->unsorted_data[it->first]->mpmt_leds.end(), it->second->mpmt_leds.begin(), it->second->mpmt_leds.end());
      msgs->m_data->unsorted_data[it->first]->mpmt_leds_mtx.unlock();
    }
    if(it->second->mpmt_waveforms.size() > 0){
      msgs->m_data->unsorted_data[it->first]->mpmt_waveforms_mtx.lock();
      msgs->m_data->unsorted_data[it->first]->mpmt_waveforms.insert( msgs->m_data->unsorted_data[it->first]->mpmt_waveforms.end(), it->second->mpmt_waveforms.begin(), it->second->mpmt_waveforms.end());
      msgs->m_data->unsorted_data[it->first]->mpmt_waveforms_mtx.unlock();
    }
    
    if(it->second->mpmt_triggers.size() > 0){
      msgs->m_data->unsorted_data[it->first]->mpmt_triggers_mtx.lock();
      msgs->m_data->unsorted_data[it->first]->mpmt_triggers.insert( msgs->m_data->unsorted_data[it->first]->mpmt_triggers.end(), it->second->mpmt_triggers.begin(), it->second->mpmt_triggers.end());
      msgs->m_data->unsorted_data[it->first]->mpmt_triggers_mtx.unlock();
      ///////////////////// send alert to evgeni for beam spill ////////////////
      
      for(int i=0; i<it->second->mpmt_triggers.size(); i++){
	std::string tmp="{\"Spill\":"+ std::to_string(msgs->m_data->spill_num)  + "}";
	if(it->second->mpmt_triggers.at(i).GetChannel()==4U) msgs->m_data->services->AlertSend("SpillCount", tmp);
      }
      ///////////////////////////
    }

    //if(it->second->mpmt_pps.size() > 0) msgs->m_data->unsorted_data[it->first]->mpmt_pps.insert( msgs->m_data->unsorted_data[it->first]->mpmt_pps.end(), it->second->mpmt_pps.begin(), it->second->mpmt_pps.end());
    if(it->second->extra_hits.size() > 0){
      msgs->m_data->unsorted_data[it->first]->extra_hits_mtx.lock();
      msgs->m_data->unsorted_data[it->first]->extra_hits.insert( msgs->m_data->unsorted_data[it->first]->extra_hits.end(), it->second->extra_hits.begin(), it->second->extra_hits.end());
      msgs->m_data->unsorted_data[it->first]->extra_hits_mtx.unlock();
    }
    if(it->second->extra_waveforms.size() > 0){
      msgs->m_data->unsorted_data[it->first]->extra_waveforms_mtx.lock();
      msgs->m_data->unsorted_data[it->first]->extra_waveforms.insert( msgs->m_data->unsorted_data[it->first]->extra_waveforms.end(), it->second->extra_waveforms.begin(), it->second->extra_waveforms.end());
      msgs->m_data->unsorted_data[it->first]->extra_waveforms_mtx.unlock();
    }
  

    delete it->second;
    it->second=0;

    
    delete msgs->m_data->unsorted_data[it->first];
    msgs->m_data->unsorted_data[it->first]=0;
    msgs->m_data->unsorted_data.erase(it->first);
    
  
} 
  //printf("d7\n");
  msgs->m_data->unsorted_data_mtx.unlock();

    


  }
  */
  //printf("d8\n");
    //printf("in send unsorted triggercard\n");
   
  //printf("d9\n");
  //  msgs->m_data->unsorted_data[bin]->Print();
  //printf("d10\n");
    //  std::stringstream tmp;
    //  tmp<<"MPMT:"<<card_id;
    //msgs->m_data->hit_map[tmp.str()]++;
  //printf("delete data\n");
  //delete msgs->daq_header;
  //msgs->daq_header=0;
  //delete msgs->mpmt_data;
  //msgs->mpmt_data=0;
  delete msgs;
  msgs=0;
  //printf("datadeleted all good\n");
  return true;
  
}
