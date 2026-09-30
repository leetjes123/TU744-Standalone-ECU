#include "logging.h"
#include "graph_math.h"
#include "calibration.h"
#include "cal_navigation.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <limits>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
// These tests exercise capture and parsing without opening a UI or file dialog.
void DrawLogViewer(LogViewerState&) {}
float g_dpiScale=1;
static bool close(float a,float b) { return std::abs(a-b)<.0001f; }
int main() {
    for(int i=0;i<SIG_COUNT;++i) {
        const auto& def=SIGNAL_DEFS[i];
        CHECK(def.id[0] && def.name[0] && def.shortName[0] && def.description[0]);
        CHECK(FindMonitorChannel(def.id)==i && FindMonitorChannel(MonitorColumnName(i))==i);
        for(int j=0;j<i;++j) CHECK(strcmp(def.id,SIGNAL_DEFS[j].id));
        std::string aliases=def.aliases;
        size_t start=0;
        while(start<aliases.size()) {
            const auto end=aliases.find('|',start);
            CHECK(FindMonitorChannel(aliases.substr(start,end-start)+"("+def.unit+")")==i);
            if(end==std::string::npos) break;
            start=end+1;
        }
    }
    CHECK(MonitorChannelMatches("Engine Speed(rpm)","RPM"));
    CHECK(MonitorChannelMatches("Coolant Temperature(C)","CLT"));
    CHECK(MonitorChannelMatches("Short Term Fuel Trim(%)","Applied trim"));
    CHECK(!GraphValueValid("AFR Actual(AFR)",0));
    CHECK(GraphValueValid("AFR Target(AFR)",0));
    for(int i=0;i<NUM_TABLES;++i) CHECK(CalibrationPageFor(ALL_TABLES[i].category,ALL_TABLES[i].name,ALL_TABLES[i].offset));
    for(int i=0;i<NUM_SCALARS;++i) CHECK(CalibrationPageFor(ALL_SCALARS[i].category,ALL_SCALARS[i].name,ALL_SCALARS[i].offset));
    for(int i=0;i<NUM_FLAGS;++i) CHECK(CalibrationPageFor(ALL_FLAGS[i].category,ALL_FLAGS[i].name,ALL_FLAGS[i].offset));
    for(int i=0;i<NUM_DROPDOWNS;++i) CHECK(CalibrationPageFor(ALL_DROPDOWNS[i].category,ALL_DROPDOWNS[i].name,ALL_DROPDOWNS[i].offset));
    CHECK(!strcmp(CALIBRATION_PAGES[0].group,"Fuel"));
    bool dtc=false;
    for(const auto& page:std::vector<CalibrationPage>(CALIBRATION_PAGES,CALIBRATION_PAGES+NUM_CALIBRATION_PAGES)) {
        if(!strcmp(page.section,"Diagnostics")) dtc=true;
        CHECK(!dtc || !strcmp(page.section,"Diagnostics"));
    }
    CHECK(CalibrationPageMatches("setup.trigger","Ignition","Trigger reference offset",0x605));
    CHECK(!CalibrationPageMatches("ignition.main","Ignition","Trigger reference offset",0x605));
    CHECK(CalibrationSearchMatches("Running VE","Fuel","Running VE",0));
    CHECK(CalibrationSearchMatches("Fuel VE Table","Fuel","Running VE",0));
    CHECK(CalibrationSearchMatches("ECT","Sensors","Coolant thermistor resistance",0x552));
    CHECK(CalibrationSearchMatches("ECT","Closed loop (STFT)","STFT minimum coolant",0));
    CHECK(!CalibrationSearchMatches("ECT","Injectors","Injector dead time",0x540));
    CHECK(!CalibrationSearchMatches("ECT","Knock","Knock window start",0xA40));
    float start=0,end=100;
    ZoomGraphWindow(100,25,.5f,start,end);
    CHECK(close(start,12.5f) && close(end,62.5f));
    start=-10; end=10; ClampGraphWindow(100,start,end);
    CHECK(close(start,0) && close(end,20));
    start=90; end=110; ClampGraphWindow(100,start,end);
    CHECK(close(start,80) && close(end,100));
    ZoomGraphWindow(.01f,0,.1f,start,end);
    CHECK(close(start,0) && close(end,.01f));
    start=std::numeric_limits<float>::quiet_NaN(); end=1;
    ClampGraphWindow(100,start,end); CHECK(start==0 && end==100);
    ClampGraphWindow(0,start,end); CHECK(start==0 && end==0);
    CHECK(NearestGraphSample({},0)==-1);
    CHECK(NearestGraphSample({0,1,3},2)==1);
    CHECK(NearestGraphSample({0,1,3},9)==2);
    const std::vector<float> times={0,1,2,3,4};
    const std::vector<float> values={0,14,16,std::numeric_limits<float>::infinity(),12};
    auto stats=SelectedGraphStatistics(times,values,"Measured AFR(AFR)",4,0);
    CHECK(stats.count==3 && stats.minimum==12 && stats.maximum==16 && stats.average()==14);
    CHECK(SelectedGraphStatistics(times,values,"RPM",0,0).count==1);
    CHECK(SelectedGraphStatistics(times,values,"RPM",9,10).count==0);

    auto log=std::make_unique<LogState>(); log->init();
    MonitorData mon{}; mon.rpm=1000; mon.measuredAfr=14.7f;
    log->pushSample(mon,100); mon.rpm=2000; log->pushSample(mon,100.07);
    log->pushSample(mon,101.2); log->refreshViewer();
    CHECK(log->viewer.parseOk && close(log->viewer.log.duration,1.2f));
    CHECK(close(log->viewer.log.time[1],.07f));
    CHECK(log->viewer.log.columnNames.size()==SIG_COUNT+1);
    log->viewer.followLatest=false;
    log->csvFile=tmpfile(); CHECK(log->csvFile);
    log->recording=true; log->writeCsvHeader();
    log->pushSample(mon,102); log->pushSample(mon,102.4); log->refreshViewer();
    CHECK(log->viewer.log.sampleCount==3 && log->sampleCount==5 && log->csvRows==2);
    CHECK(close(log->recordingElapsed,.4f));
    log->clear(); CHECK(log->recording && log->sampleCount==0);
    log->pushSample(mon,103); log->pushSample(mon,103.25); log->refreshViewer();
    CHECK(log->viewer.parseOk && log->viewer.log.sampleCount==2);
    CHECK(log->viewer.log.columnNames.size()==SIG_COUNT+1);
    CHECK(close(log->recordingElapsed,1.25f));
    fflush(log->csvFile); rewind(log->csvFile);
    char line[4096]; CHECK(fgets(line,sizeof(line),log->csvFile));
    const float expected[]={0,.4f,1,1.25f};
    for(float t:expected) { CHECK(fgets(line,sizeof(line),log->csvFile)); CHECK(close(strtof(line,nullptr),t)); }
    log->stopRecording();
    log->clear();
    for(int i=0;i<LOG_MAX_SAMPLES+10;++i) { mon.rpm=i; log->pushSample(mon,200+i*.05); }
    log->refreshViewer();
    CHECK(log->sampleCount==LOG_MAX_SAMPLES);
    CHECK(close(log->viewer.log.time.front(),0));
    CHECK(log->viewer.log.columns[SIG_RPM+1].front()==10);
    CHECK(close(log->viewer.log.duration,(LOG_MAX_SAMPLES-1)*.05f));
    log->pushSample(mon,1); // An erroneous timestamp must never reverse history.
    CHECK(log->sampleTimes[(log->writeIdx+LOG_MAX_SAMPLES-1)%LOG_MAX_SAMPLES]>200+(LOG_MAX_SAMPLES+9)*.05);

    const char* path="graph-parser-test.csv";
    FILE* csv=fopen(path,"w"); CHECK(csv);
    fputs("Time(s),RPM(rpm),Measured AFR(AFR)\n10,1000,0\n10.1,2000,14\n10.1,3000,15\n10.2,nan,16\n10.3,4000,16\n11,5000\n",csv); fclose(csv);
    LogViewerState parsed; CHECK(ParseLogFile(path,parsed));
    CHECK(parsed.log.sampleCount==3 && parsed.log.skippedRows==3 && parsed.log.diagnostics.size()==3);
    CHECK(close(parsed.log.duration,.3f));
    CHECK(parsed.signals[1].fullMin==14 && parsed.signals[1].fullMax==16);
    CHECK(parsed.log.columnNames[1]=="Engine Speed(rpm)" && parsed.signals[0].visible);
    CHECK(parsed.signals[0].color==SIGNAL_DEFS[SIG_RPM].color);
    csv=fopen(path,"w"); CHECK(csv);
    log->clear(); log->csvFile=csv; log->recording=true; log->recordingBaseTime=-1; log->writeCsvHeader();
    mon.rpm=1234; mon.measuredAfr=14; mon.knockAvailable=false;
    log->pushSample(mon,1000); mon.rpm=2345; log->pushSample(mon,1000.1);
    log->stopRecording();
    CHECK(ParseLogFile(path,parsed) && parsed.log.sampleCount==2 && parsed.log.skippedRows==0);
    CHECK(parsed.log.columnNames[SIG_RPM+1]==MonitorColumnName(SIG_RPM));
    CHECK(parsed.log.columns[SIG_RPM+1][0]==1234 && parsed.log.columns[SIG_RPM+1][1]==2345);
    CHECK(!std::isfinite(parsed.log.columns[SIG_KNOCK_MV+1][0]));
    CHECK(parsed.signals[SIG_STFT].visible);
    csv=fopen(path,"w"); CHECK(csv); fputs("Time,RPM\n0,1000\n0,2000\n",csv); fclose(csv);
    CHECK(!ParseLogFile(path,parsed) && !parsed.parseError.empty());
    remove(path);
    puts("PASS channel names/aliases, calibration coverage/order/search, CSV round trip, graph windows, statistics, timestamps, paused capture, recording, reset and rollover");
}
