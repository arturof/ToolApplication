#include "VMETest.h"
#include "DataModel.h"

template <typename Hit>
void generate_readout(VMEReadout<Hit>& readout, int nevents, int nhits) {
  std::deque<typename VMEReadout<Hit>::Event> events;
  for (int i = 0; i < nevents; ++i)
    events.push_back(
        {
          std::chrono::time_point_cast<std::chrono::milliseconds>(
              VMEReadout<Hit>::Time::clock::now()
          ),
          std::vector<Hit>(nhits)
        }
    );
  readout.push(events.begin(), events.end());
};

bool VMETest::Initialise(std::string configfile, DataModel& data) {
  try {
    InitialiseTool(data);
    InitialiseConfiguration(std::move(configfile));

    if (!m_variables.Get("verbose", m_verbose)) m_verbose = 3;

    m_data->services->AddSlowControlVariable(
        "start",
        BUTTON,
        [this](const char*) -> std::string {
          auto start = boost::posix_time::microsec_clock::universal_time()
                     + boost::posix_time::seconds(5);
          *m_log << ML(3) << "RunStart " << start << std::endl;
          m_data->services->AlertSend(
              "RunStart",
              "{\"Timestamp\":"
              + std::to_string(
                  boost::posix_time::time_duration(
                    start
                    - boost::posix_time::time_from_string(
                        "1970-01-01 00:00:00.000"
                      )
                  ).total_seconds()
                )
              + "}"
          );
          return "ok";
        }
    );

    m_data->services->AddSlowControlVariable(
        "stop",
        BUTTON,
        [this](const char*) -> std::string {
        *m_log << ML(3) << "RunStop" << std::endl;
          m_data->services->AlertSend("RunStop", "{}");
          return "ok";
        }
    );

    m_data->services->AddSlowControlVariable(
        "reconfigure",
        BUTTON,
        [this](const char*) -> std::string {
          *m_log << ML(3) << "ChangeConfig" << std::endl;
          m_data->services->AlertSend("ChangeConfig", "{\"RunConfig\":\"0\"}");
          return "ok";
        }
    );

    return true;
  } catch (std::exception& e) {
    if (m_log)
      *m_log << ML(0) << e.what() << std::endl;
    else
      fprintf(stderr, "%s\n", e.what());
    return false;
  };
};

void VMETest::start_generation() {
  stop = false;
  thread = std::thread(
      [this]() {
        bool warned = false;
        while (!stop) {
          if (m_data->qdc_readout.size() > 1000) {
            if (!warned) {
              fprintf(stderr, "Generation overflow\n");
              warned = true;
            };
            return;
          };
          warned = false;
          generate_readout(m_data->qdc_readout, 10, 20);
          generate_readout(m_data->tdc_readout, 10, 70);
          usleep(1000);
        }
      }
  );
};

void VMETest::stop_generation() {
  stop = true;
  thread.join();
};

bool VMETest::Execute() {
  try {
    if (m_data->run_start) {
      *m_log << ML(3) << "Starting run" << std::endl;
      if (stop) start_generation();
    };

    if (m_data->run_stop) {
      *m_log << ML(3) << "Stopping run" << std::endl;
      if (!stop) stop_generation();
    };

    if (m_data->change_config)
      *m_log << ML(3) << "Changing configuration" << std::endl;

    *m_log << ML(3) << m_data->run_start << ' ' << m_data->run_stop << ' ' << m_data->change_config << std::endl;

    return true;
  } catch (std::exception& e) {
    *m_log << ML(0) << e.what() << std::endl;
    return false;
  };
};

bool VMETest::Finalise() {
  try {
    if (thread.joinable()) {
      stop = true;
      thread.join();
    };
    return true;
  } catch (std::exception& e) {
    *m_log << ML(0) << e.what() << std::endl;
    return false;
  };
};
