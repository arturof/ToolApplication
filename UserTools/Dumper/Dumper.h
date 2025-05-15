#ifndef DUMPER_H
#define DUMPER_H

#include <fstream>
#include <memory>

#include "Tool.h"
#include "Utilities.h"

class Dumper: public ToolFramework::Tool {
  public:
    bool Initialise(std::string configfile, DataModel& data);
    bool Execute();
    bool Finalise();

  private:
    struct Thread: ToolFramework::Thread_args {
      Dumper& tool;

      Thread(Dumper& dumper): tool(dumper) {};
    };

    std::ofstream tdc, qdc;
    std::unique_ptr<Thread> thread;
    ToolFramework::Utilities util;

    void open(std::ofstream& stream, const std::string& variable);
    void dump();
    int run;

    static void dumper_thread(ToolFramework::Thread_args* args);
};

#endif
