﻿#include "userlogmanager.h"
#include "msgprocess.h"


UserLogManager::UserLogManager()
{

}

void UserLogManager::checkTable(){
    if(!g_db.tableExists("agv_log")){
        g_db.execDML("create table agv_log(id INTEGER primary key AUTOINCREMENT, log_time,log_msg char(1024));");
    }
}


void UserLogManager::init()
{
    checkTable();

    g_threadPool.enqueue([&]{
        while(true){
            if(logQueue.empty())
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            mtx.lock();
            USER_LOG log = logQueue.front();
            logQueue.pop();
            mtx.unlock();

            //1.存库
            try{
                std::stringstream ss;
                ss<<"insert into agv_log (log_time,log_msg) values ("<<log.time<<" ,"<<log.msg<<" );";
                g_db.execDML(ss.str().c_str());
            }catch(CppSQLite3Exception &e){
                LOG(ERROR) << e.errorCode() << ":" << e.errorMessage();
            }catch(std::exception e){
                LOG(ERROR) << e.what();
            }
            //2.发布
            MsgProcess::getInstance()->publishOneLog(log);
        }
    });
}

void UserLogManager::push(const std::string &s)
{
    USER_LOG log;
    log.time = getTimeStrNow();
    log.msg = s;
    mtx.lock();
    logQueue.push(log);
    mtx.unlock();
}

void UserLogManager::interLogDuring(qyhnetwork::TcpSessionPtr conn,const Json::Value &request)
{
	Json::Value response;
	response["type"] = MSG_TYPE_RESPONSE;
	response["todo"] = request["todo"];
	response["queuenumber"] = request["queuenumber"];
	response["result"] = RETURN_MSG_RESULT_SUCCESS;
	if (request["startTime"].isNull() ||
		request["endTime"].isNull()) {
		response["result"] = RETURN_MSG_RESULT_FAIL;
		response["error_code"] = RETURN_MSG_ERROR_CODE_PARAMS;
	}else{
        std::string startTime = request["startTime"].asString();
        std::string endTime = request["endTime"].asString();
        push(conn->getUserName()+"查询历史日志，时间是从"+startTime+" 到"+endTime);
		Json::Value agv_logs;
        try{
            std::stringstream ss;
            ss<<"select log_time,log_msg from agv_log where log_time >= \'"<<startTime<<"\' and log_time<=\'"<<endTime<<"\' ;";
            CppSQLite3Table table = g_db.getTable(ss.str().c_str());
            if(table.numRows()>0 && table.numFields() == 2 ){
                for(int i=0;i<table.numRows();++i){
                    table.setRow(i);
					Json::Value agv_log;
					agv_log["time"] = std::string(table.fieldValue(0));
					agv_log["msg"] = std::string(table.fieldValue(1));
					agv_logs.append(agv_log);
                }
            }
			response["logs"] = agv_logs;
        }
		catch (CppSQLite3Exception e) {
			response["result"] = RETURN_MSG_RESULT_FAIL;
			response["error_code"] = RETURN_MSG_ERROR_CODE_QUERY_SQL_FAIL;
			std::stringstream ss;
			ss << "code:" << e.errorCode() << " msg:" << e.errorMessage();
			response["error_info"] = ss.str();
			LOG(ERROR) << "sqlerr code:" << e.errorCode() << " msg:" << e.errorMessage();
		}
		catch (std::exception e) {
			response["result"] = RETURN_MSG_RESULT_FAIL;
			response["error_code"] = RETURN_MSG_ERROR_CODE_QUERY_SQL_FAIL;
			std::stringstream ss;
			ss << "info:" << e.what();
			response["error_info"] = ss.str();
			LOG(ERROR) << "sqlerr code:" << e.what();
		}
    }

    conn->send(response);
}
