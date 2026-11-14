#pragma once
// Diagnostic I/O must never run on a render/physics callback.
static bool worldLogEnabled=false;
static DWORD worldLogFlushMs=10000,worldLogMaxLines=20;
static HANDLE worldLogWake=nullptr,worldLogThread=nullptr;
static volatile LONG worldLogQuit=0,worldLogDropped=0;
static std::mutex worldLogMutex;
static std::deque<std::string> worldLogQueue;
static size_t worldLogBytes=0;
static constexpr size_t worldLogMaxBytes=256*1024;
static DWORD WINAPI WorldLogWriter(void*){
 for(;;){
  WaitForSingleObject(worldLogWake,worldLogFlushMs);
  std::deque<std::string> records;
  {std::lock_guard lock(worldLogMutex);records.swap(worldLogQueue);worldLogBytes=0;}
  for(const auto& record:records)fwrite(record.data(),1,record.size(),logFile);
  LONG dropped=InterlockedExchange(&worldLogDropped,0);
  if(dropped)fprintf(logFile,"%lu DEBUG_LOG_DROPPED count=%ld bounded_queue=1\n",GetTickCount(),dropped);
  if(!records.empty()||dropped)fflush(logFile);
  if(InterlockedCompareExchange(&worldLogQuit,0,0))break;
 }
 fclose(logFile);logFile=nullptr;return 0;
}
static void WorldDebugLog(const char* fmt,...){
 if(!worldLogEnabled)return;
 std::unique_lock<std::mutex> lock(worldLogMutex,std::try_to_lock);
 if(!lock.owns_lock()){InterlockedIncrement(&worldLogDropped);return;}
 DWORD now=GetTickCount();static DWORD window=0;static unsigned lines=0;
 if(now-window>=1000){window=now;lines=0;}
 const bool important=strstr(fmt,"FAULT")||strstr(fmt,"REFUSED")||strstr(fmt,"ACTIVE");
 if(!important&&worldLogMaxLines&&lines>=worldLogMaxLines){InterlockedIncrement(&worldLogDropped);return;}
 // Bound both formatting work and memory even if storage stalls indefinitely.
 char buffer[2048];int prefix=sprintf_s(buffer,"%lu ",now);
 va_list args;va_start(args,fmt);_vsnprintf_s(buffer+prefix,sizeof(buffer)-prefix-2,_TRUNCATE,fmt,args);va_end(args);
 size_t length=strnlen_s(buffer,sizeof(buffer)-2);buffer[length++]='\n';
 if(worldLogBytes+length>worldLogMaxBytes){InterlockedIncrement(&worldLogDropped);return;}
 worldLogQueue.emplace_back(buffer,length);worldLogBytes+=length;++lines;
}
// OFF bypasses evaluation of diagnostic arguments as well as all I/O/locking.
#define Log(...) do{if(worldLogEnabled)WorldDebugLog(__VA_ARGS__);}while(0)
static bool WorldDiagnosticFlag(const char* file,const char* section,const char* key){if(!worldLogEnabled)return false;char value[8]{};GetPrivateProfileStringA(section,key,"0",value,sizeof(value),file);return !strcmp(value,"1");}
static void InitWorldDebugLog(const char* ini,const char* output){
 char loggingSetting[8]{};
 GetPrivateProfileStringA("Debug","Logging","0",loggingSetting,sizeof(loggingSetting),ini);
 if(strcmp(loggingSetting,"1")!=0)return;
 worldLogFlushMs=std::clamp<DWORD>(GetPrivateProfileIntA("Debug","LogFlushIntervalMs",10000,ini),1000,60000);
 worldLogMaxLines=std::clamp<DWORD>(GetPrivateProfileIntA("Debug","LogMaxLinesPerSecond",20,ini),0,10000);
 logFile=_fsopen(output,"w",_SH_DENYNO);if(!logFile)return;
 setvbuf(logFile,nullptr,_IOFBF,65536);
 worldLogWake=CreateEventA(nullptr,FALSE,FALSE,nullptr);
 if(worldLogWake)worldLogThread=CreateThread(nullptr,0,WorldLogWriter,nullptr,0,nullptr);
 if(!worldLogThread){if(worldLogWake)CloseHandle(worldLogWake);worldLogWake=nullptr;fclose(logFile);logFile=nullptr;return;}
 worldLogEnabled=true;
 Log("DEBUG_LOG_ACTIVE async=1 flush_ms=%u max_lines_per_second=%u queue_bytes=%u",unsigned(worldLogFlushMs),unsigned(worldLogMaxLines),unsigned(worldLogMaxBytes));
}
