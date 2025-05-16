#include "FileWriter.h"

FileWriter_args::FileWriter_args():Thread_args(){
  data=0;
  file_name=0;
  part_number=0;
  file_writeout_period=0;
}

FileWriter_args::~FileWriter_args(){
  data=0;
  file_name=0;
  part_number=0;
  file_writeout_period=0;
}


FileWriter::FileWriter():Tool(){}


bool FileWriter::Initialise(std::string configfile, DataModel &data){

  InitialiseTool(data);
  m_configfile=configfile;
  InitialiseConfiguration(configfile);
  //m_variables.Print();

  m_util=new Utilities();
  args=new FileWriter_args();
  
  LoadConfig();
  
  args->data= m_data;
  args->file_name= &m_file_name;
  args->part_number= &m_part_number;
  args->file_writeout_period= & m_file_writeout_period;
  
  m_util->CreateThread("test", &Thread, args);

  ExportConfiguration();
  
  return true;
}


bool FileWriter::Execute(){

  if(m_data->change_config){
    InitialiseConfiguration(m_configfile); // surely add load config here   need to do return checks.
    
    ExportConfiguration();
  }
  if(m_data->run_start) LoadConfig();   ///?   oh maybe to ensure file file written before load config happends but this is a crap way of doing it please change Ben
  if(m_data->run_stop) args->period=boost::posix_time::seconds(10);

  m_data->vars.Set("part",m_part_number);
  
  return true;
}


bool FileWriter::Finalise(){

  m_util->KillThread(args);

  delete args;
  args=0;

  delete m_util;
  m_util=0;

  return true;
}

void FileWriter::Thread(Thread_args* arg){

  FileWriter_args* args=reinterpret_cast<FileWriter_args*>(arg);

  args->lapse = args->period -( boost::posix_time::microsec_clock::universal_time() - args->last);
  if(!args->lapse.is_negative()){
    usleep(100);
    return;
  }
  //printf("%d\n", *(args->part_number));
  //  if(*args->file_writeout_period==0){
  // *args->file_writeout_period=1;
  //}
  
  args->last= boost::posix_time::microsec_clock::universal_time();
  
  args->data->readout_windows_mtx.lock();

  if(args->data->readout_windows->size()==0){
    args->data->readout_windows_mtx.unlock();
    return;
  }

  printf("writing out data\n");
  std::deque<ReadoutWindow*>* readout_windows= args->data->readout_windows;
  args->data->readout_windows= new std::deque<ReadoutWindow*>;
  args->data->readout_windows_mtx.unlock();
   
  std::stringstream filename;
  filename<<(*args->file_name)<<"R"<<args->data->run_number<<"S"<<args->data->sub_run_number<<"P"<<(*args->part_number)<<".dat";
  BinaryStream output;
  output.Bopen(filename.str().c_str(), NEW, UNCOMPRESSED);
  
  WCTERawData tmp;
  
  tmp.readout_windows.reserve(readout_windows->size());
  printf("readout_windows->size()=%lu\n",readout_windows->size());
  for(unsigned int i=0; i<readout_windows->size(); i++){
    if((readout_windows->at(i))->triggers_info.size() ==0)printf("eeror 1 in file\n");
    //printf("d1\n");
    tmp.readout_windows.push_back(*(readout_windows->at(i)));
      delete readout_windows->at(i);
      readout_windows->at(i)=0;
  }
  output<<tmp;
  
  delete readout_windows;
  readout_windows=0;
  
    //printf("d2\n");
  output.Bclose();
  (*args->part_number)++;
  
}

void FileWriter::LoadConfig(){ // change to bool have a return type

  
  if(!m_variables.Get("verbose",m_verbose)) m_verbose=1;
  if(!m_variables.Get("file_path",m_file_name)) m_file_name="./data";
  if(!m_variables.Get("file_writeout_period",m_file_writeout_period)) m_file_writeout_period=300;
  
  m_part_number=0;
  args->last=boost::posix_time::microsec_clock::universal_time();
  args->period=boost::posix_time::seconds(m_file_writeout_period);
  
}
