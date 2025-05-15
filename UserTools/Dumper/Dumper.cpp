#include "DataModel.h"

#include "Dumper.h"

template <typename Hit>
static void dump(VMEReadout<Hit>& readout, std::ofstream& stream) {
  auto r = readout.get();
  for (auto& event : r) {
    uint64_t time = event.time.time_since_epoch().count();
    stream.write(reinterpret_cast<char*>(&time), sizeof(time));

    uint8_t size = event.hits.size();
    stream.write(reinterpret_cast<char*>(&size), sizeof(size));

    stream.write(
        reinterpret_cast<char*>(event.hits.data()),
        sizeof(*event.hits.data()) * size
    );
  };
};

void Dumper::dump() {
  bool qdc_ = m_data->qdc_readout.size();
  bool tdc_ = m_data->tdc_readout.size();
  if (!qdc_ && !tdc_) {
    usleep(100);
    return;
  };

  if (qdc_) ::dump(m_data->qdc_readout, qdc);
  if (tdc_) ::dump(m_data->tdc_readout, tdc);
};

void Dumper::dumper_thread(ToolFramework::Thread_args* args) {
  Thread* thread = static_cast<Thread*>(args);
  Dumper& tool = thread->tool;
  try {
    tool.dump();
  } catch (std::exception& e) {
    *tool.m_log << tool.ML(0) << e.what() << std::endl;
    thread->kill = true;
  };
};

void Dumper::open(std::ofstream& stream, const std::string& var) {

  // open run summary file to get the next run number
  std::string last_line;
  std::ifstream fruns("run_summary.txt");
  if (fruns && fruns.is_open()) {
    std::string line;
    while (getline(fruns,line)) {
      last_line = line;
    }
    fruns.close();
  }
  else {
    std::cout << "Couldn't open run summary file" << std::endl;
  }

  // output file name with run number
  std::stringstream ssi(last_line);
  run = 0;
  ssi >> run;
  run++;
  std::stringstream sso;
  sso << std::setw(4) << std::setfill('0') << run;

  std::string filename;
  if (!m_variables.Get(var, filename)) filename = var + ".out";
  filename += "_run" + sso.str() + ".out";
  *m_log << ML(0) << "Output filename " << filename << std::endl;

  // open outfile file only if it doesn't exist
  std::ifstream check(filename);
  if (check.is_open()) {
    check.close();
    *m_log << ML(0) << "Output filename " << filename << " already exists" << std::endl;
  }
  else {
    stream.open(filename, std::ios::binary | std::ios::out);
  }

  if (!stream)
    throw std::runtime_error(
        std::string("Cannot open ")
        + var
        + " output file `"
        + filename
        + "': "
        + strerror(errno)
    );
};

bool Dumper::Initialise(std::string configfile, DataModel& data) {
  try {
    InitialiseTool(data);
    InitialiseConfiguration(configfile);

    if (!m_variables.Get("verbose", m_verbose)) m_verbose = 1;

    // open output files
    open(tdc, "tdc");
    open(qdc, "qdc");

    // update run summary file
    std::ofstream fruns("run_summary.txt", std::ios::app);
    if (fruns && fruns.is_open()) {
      fruns << std::setw(4) << std::setfill('0') << run << std::endl;
      fruns.close();
    }
    else {
      std::cout << "Couldn't open run summary file" << std::endl;
    }
    std::cout << "Summary file updated with run " << run << std::endl;

    // print run start time
    auto start = std::chrono::system_clock::now();
    std::time_t start_time = std::chrono::system_clock::to_time_t(start);
    std::cout << "Run initialised on " << std::ctime(&start_time) << std::endl;

    ExportConfiguration();

    if (!tdc.is_open()) return false;
    if (!qdc.is_open()) return false;

    return true;

  } catch (std::exception& e) {
    if (m_log)
      *m_log << ML(0) << e.what() << std::endl;
    else
      fprintf(stderr, "%s\n", e.what());
    return false;
  };
};

bool Dumper::Execute() {
  if (!tdc.is_open() || !qdc.is_open()) return false;
  if (thread) return true;

  try {
    thread.reset(new Thread(*this));
    util.CreateThread("Dumper", &dumper_thread, thread.get());
    return true;
  } catch (std::exception& e) {
    *m_log << ML(0) << e.what() << std::endl;
    return false;
  };
};

bool Dumper::Finalise() {
  try {
    if (thread) {
      util.KillThread(thread.get());
      delete thread.release();
    };
    if (tdc) tdc.close();
    if (qdc) qdc.close();

    // print run stop time
    auto end = std::chrono::system_clock::now();
    std::time_t end_time = std::chrono::system_clock::to_time_t(end);
    std::cout << "Run finalised on " << std::ctime(&end_time) << std::endl;

    return true;
  } catch (std::exception& e) {
    *m_log << ML(0) << e.what() << std::endl;
    return false;
  };
};
