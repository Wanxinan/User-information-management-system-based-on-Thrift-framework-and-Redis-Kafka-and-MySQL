#include "gen-cpp/UserService.h"
#include <mysql/mysql.h>
#include <iostream>
#include <sstream>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <cstring>
#include <hiredis/hiredis.h>
#include <jsoncpp/json/json.h>
#include <librdkafka/rdkafkacpp.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>

using namespace std;
using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;

class IDGenerator {
private:
    atomic<int> counter;

public:
    IDGenerator() {
        counter.store(0);
    }

    int ID() {
        int time_suffix = time(nullptr) % 100;      
        int cnt = counter.fetch_add(1) % 10000; 
        return time_suffix * 10000 + cnt;            
    }
};

class UserServiceHandler : virtual public UserServiceIf {
public:
    UserServiceHandler() 
    :mysql_conn(nullptr)
    ,redis_conn(nullptr)
    ,kafka_producer(nullptr)
    {
        //MySQL初始化
        mysql_conn = mysql_init(nullptr);
        if(!mysql_real_connect(mysql_conn,"localhost","root","Wb20010417.","user_db",0,NULL,0)){
            throw runtime_error(mysql_error(mysql_conn));
        }

        //创建用户表
        const char* create_table = R"(
            CREATE TABLE IF NOT EXISTS users(
            id BIGINT PRIMARY KEY,
            username VARCHAR(50) NOT NULL,
            gender TINYINT NOT NULL,
            age INT,
            phone VARCHAR(20),
            email VARCHAR(100),
            description TEXT)
        )";

        if(mysql_query(mysql_conn,create_table)){
            throw runtime_error("Failed to create table:users "+ string(mysql_error(mysql_conn)));
        }

        //redis初始化
        redis_conn = redisConnect("127.0.0.1",6379);
        if(redis_conn==nullptr || redis_conn->err){
            if(redis_conn){
                throw runtime_error("Redis connection failed: "+ string(redis_conn->errstr));
            }else{
                throw runtime_error("Can't allocate Redis context");
            }
        }

        //Kafka生产者初始化
        string errstr;
        RdKafka::Conf *conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
        if(conf->set("bootstrap.servers","localhost:9092",errstr)!=RdKafka::Conf::CONF_OK){
            throw runtime_error("Kafka config error:" + errstr);
        }

        kafka_producer = RdKafka::Producer::create(conf,errstr);
        if(!kafka_producer){
            throw runtime_error("Failed to create Kafka producer:"+ errstr);
        }

        delete conf;

        RdKafka::Conf *tconf = RdKafka::Conf::create(RdKafka::Conf::CONF_TOPIC);
        this->topic = RdKafka::Topic::create(kafka_producer,"user_updates",tconf,errstr);
        if (!this->topic)
        {
            cerr<<"create Kafka_topic failed. "<<errstr<<endl;
            delete tconf;
            throw runtime_error("Kafka topic creation failed");
        }
        delete tconf;
    }

    ~UserServiceHandler(){
        if(mysql_conn) 
            mysql_close(mysql_conn);
        if(redis_conn) 
            redisFree(redis_conn);
        if(kafka_producer)
            delete kafka_producer;
        if(topic)
            delete topic;
    }

    void createUsers(User& _return, const User& user) {
        
        int64_t new_id = id_generator.ID();

        //使用存储过程创建用户
        vector<string> params;
        params.push_back(to_string(new_id)); 
        params.push_back("'"+escapeSQL(user.username)+"'");
        params.push_back(to_string(static_cast<int>(user.gender)));
        params.push_back(to_string(user.age));
        params.push_back("'" + escapeSQL(user.phone)+"'");
        params.push_back("'" + escapeSQL(user.email)+"'");
        params.push_back("'" + escapeSQL(user.description)+"'");

        cout<<new_id<<endl;
        
        // //
        // printUser(user);
        
        CallMysqlProcedure("CreateUser",params);

        _return = user;
        _return.id = new_id; //服务端分配id
        _return.__isset.id = true;

        //设置redis缓存
        SetRedisUser(_return);

        //发送kafka消息
        SendKafkaMessage(_return,"CREATE");        
    }

    void getUserInfo(User& _return, const int64_t id) {
        //先尝试从redis获取,有的话直接return
        if(getRedisUser(id,_return)){
            return;
        }        

        //没有的话从mysql查，用Mysql存储过程语句
        getUserFromProcedure(id,_return);

        //查到后写入redis
        SetRedisUser(_return);

        // //发送kafka消息 GET不涉及消息变化
        // SendKafkaMessage(_return,"GET");      
    }

    void updateUserInfo(const User& user) {
        // 检查字段mask
        if (!user.__isset.field_mask || user.field_mask.empty()) {
            return; // 没有需要更新的字段
        }
        
        if(user.id == 0){
            UserException e;
            e.errorCode = 400;
            e.message = "User id must be provided for update.";
            throw e;
        }

        // 获取当前用户信息
        User current;
        getUserInfo(current, user.id);
        cout<<"获取当前用户信息成功！"<<endl;

        // 构建更新参数
        vector<string> params;
        params.push_back(to_string(user.id));
        
        // 只更新标记的字段
        const auto& mask = user.field_mask;
        string updateFields = "";
        
        if (mask.find("username") != mask.end() && mask.at("username")) {
            current.username = user.username;
            updateFields += "username,";
        }
        params.push_back("'" + escapeSQL(current.username) + "'");
        
        if (mask.find("gender") != mask.end() && mask.at("gender")) {
            current.gender = user.gender;
            updateFields += "gender,";
        }
        params.push_back(to_string(static_cast<int>(current.gender)));
        
        if (mask.find("age") != mask.end() && mask.at("age")) {
            current.age = user.age;
            updateFields += "age,";
        }
        params.push_back(to_string(current.age));
        
        if (mask.find("phone") != mask.end() && mask.at("phone")) {
            current.phone = user.phone;
            updateFields += "phone,";
        }
        params.push_back("'" + escapeSQL(current.phone) + "'");
        
        if (mask.find("email") != mask.end() && mask.at("email")) {
            current.email = user.email;
            updateFields += "email,";
        }
        params.push_back("'" + escapeSQL(current.email) + "'");
        
        if (mask.find("description") != mask.end() && mask.at("description")) {
            current.description = user.description;
            updateFields += "description,";
        }
        params.push_back("'" + escapeSQL(current.description) + "'");

        // 调用存储过程
        CallMysqlProcedure("UpdateUser", params);
        
        // 更新缓存和发送消息
        SetRedisUser(current);
        SendKafkaMessage(current, "UPDATE");
        
        // 打印更新字段
        if (!updateFields.empty()) {
            updateFields.pop_back();
            cout << "Updated fields: " << updateFields << endl;
        }
    }
 
private:
    MYSQL* mysql_conn;
    redisContext* redis_conn;
    RdKafka::Producer* kafka_producer;
    RdKafka::Topic* topic;
    static IDGenerator id_generator;

    //打印调试
    void printUser(const User& user){
    cout<<"User ID: "<<user.id<<"\n"
        <<"User Name: "<<user.username<<"\n"
        <<"Gender: ";
    switch (user.gender)
    {   
        case Gender::MALE: 
            cout<<"MALE";
            break;
        case Gender::FEMALE: 
            cout<<"FEMALE";
            break;
        case Gender::OTHER: 
            cout<<"OTHER";
            break;
        default:
            cout<<"Unknow";
    }
    cout<<"\n"
        <<"Age: "<<user.age<<"\n"
        <<"Phone: "<<user.phone<<"\n"
        <<"Email: "<<user.email<<"\n"
        <<"Description: "<<user.description<<endl;
}

    //发送消息到kafka
    void SendKafkaMessage(const User& user,const string& action){
        //.thrift 里的转到json
        Json::Value root;
        root["action"] = action;
        root["id"] = (Json::Int64)user.id;
        root["name"] = user.username;
        root["gender"] = static_cast<int>(user.gender);
        root["age"] = user.age;
        root["phone"] = user.phone;
        root["email"] = user.email;
        root["description"] = user.description;

        Json::FastWriter writer;
        string message = writer.write(root);

        RdKafka::ErrorCode resp = kafka_producer->produce(
            "user_updates",
            RdKafka::Topic::PARTITION_UA,
            RdKafka::Producer::RK_MSG_COPY,
            const_cast<char*>(message.c_str()),
            message.size(),
            NULL,0,0,NULL
        );
        
        if(resp != RdKafka::ERR_NO_ERROR){
            cerr<<"Rdkafka producer faile: "<<RdKafka::err2str(resp)<<endl;
        }else{
            kafka_producer->poll(0);
        }
    }

    //将user对象写入redis缓存
    void SetRedisUser(const User& user){
        string key = "user:" + to_string(user.id);
        redisCommand(redis_conn,"DEL %s",key.c_str());

        //确保性别有效
        int gender_val = static_cast<int>(user.gender);
        if(gender_val<1||gender_val >3){
            gender_val = static_cast<int>(Gender::OTHER);
        }

        redisReply* reply = (redisReply*)redisCommand(redis_conn,
            "HSET %s username %s gender %d age %d phone %s email %s description %s",
            key.c_str(),
            user.username.c_str(),
            user.gender,
            user.age,
            user.phone.c_str(),
            user.email.c_str(),
            user.description.c_str());

        freeReplyObject(reply);
        redisCommand(redis_conn,"EXPIRE %s 3600",key.c_str());
    }                                       

    //取redis缓存
    bool getRedisUser(int64_t id,User& user){
        string key = "user:" + to_string(id);
        redisReply* reply = (redisReply*)redisCommand(redis_conn,"HGETALL %s",key.c_str());

        if(reply->type != REDIS_REPLY_ARRAY || reply->elements == 0){
            freeReplyObject(reply);
            return false;
        }

        user.id = id;

        //取字段和value
        for(size_t i = 0; i < reply->elements; i += 2){
            string field = reply->element[i]->str;
            string value = reply->element[i+1]->str;

            if(field == "username")
                user.username = value;
            else if(field == "gender"){
                if(!value.empty() && value != "0"){
                    user.gender = IntToGender(stoi(value));
                }else{
                    user.gender = Gender::OTHER;
                }
            }
            else if(field == "age")
                user.age = stoi(value);
            else if(field == "phone")
                user.phone = value;
            else if(field == "email")
                user.email = value;
            else if(field == "description")
                user.description = value;
        }
    
        freeReplyObject(reply);
        return true;
    }

  
    //调用Mysql存储过程
    void CallMysqlProcedure(const string& procedure,const vector<string>& params){

        string query = "CALL " + procedure + "(";
        for(size_t i = 0; i < params.size(); i++){
            query += params[i];
            if(i < params.size() - 1)
            query += ",";
        }
        query += ")";
        
        if(mysql_query(mysql_conn, query.c_str())) {
            // 即使失败，也必须清理一下
            while (mysql_next_result(mysql_conn) == 0) {
                MYSQL_RES* result = mysql_store_result(mysql_conn);
                if (result) mysql_free_result(result);
            }

            UserException e;
            e.errorCode = 500;
            e.message = "Mysql error: " + string(mysql_error(mysql_conn));
            throw e;
        }

        do{
            MYSQL_RES* result = mysql_store_result(mysql_conn);
            if(result)
                mysql_free_result(result);
        }while(mysql_next_result(mysql_conn)==0);
    }

    //存储过程结果中获取用户
    void getUserFromProcedure(int64_t id,User& user){
        while (mysql_next_result(mysql_conn) == 0) {
            MYSQL_RES* res = mysql_store_result(mysql_conn);
            if (res) mysql_free_result(res);
        }

        string query = "CALL GetUser(" + to_string(id) + ")";
            
        if(mysql_query(mysql_conn,query.c_str())){

            UserException e;
            e.errorCode = 500;
            e.message = "Mysql error:"+ string(mysql_error(mysql_conn));
            throw e;
        }

        MYSQL_RES* result = nullptr;
        MYSQL_ROW row = nullptr;
        bool found = false;
    
        // 循环所有结果集，直到找到有数据的
        int result_count = 0;
        do {
            result = mysql_store_result(mysql_conn);
            cout << "[DEBUG] result_count: " << result_count << ", rows: " << (result ? mysql_num_rows(result) : -1) << endl;
            if (result) {
                my_ulonglong rows = mysql_num_rows(result);
                if (rows > 0) {
                    row = mysql_fetch_row(result);
                    found = true;
                    break;
                }
                mysql_free_result(result);
            }
            result_count++;
        } while (mysql_next_result(mysql_conn) == 0);
    
        if (!found || !row) {
            if (result) mysql_free_result(result);
            UserException e;
            e.errorCode = 404;
            e.message = "User not found.";
            throw e;
        }

        //三元运算符第三个数统一使用整数
        user.id = id;
        user.username = row[1] ? row[1] : "";
        user.gender = IntToGender(stoi(row[2]));
        user.age = row[3] ? stoi(row[3]): 0;
        user.phone = row[4] ? row[4] : "";
        user.email = row[5] ? row[5] : "";
        user.description = row[6] ? row[6] : "";

        mysql_free_result(result);

        // 清理后续结果集
        while (mysql_next_result(mysql_conn) == 0) {
            MYSQL_RES* extraRes = mysql_store_result(mysql_conn);
            if (extraRes) mysql_free_result(extraRes);
        }
    }

    //防sql注入
    string escapeSQL(const string& input){
        char* output = new char[input.length()*2+1];
        mysql_real_escape_string(mysql_conn,output,input.c_str(),input.length());
        string result(output);
        delete[] output;
        return result;
    }

    //Gender转int 这里是Gender::type
    Gender::type IntToGender(int value){
        switch(value){
            case 1: return Gender::MALE;
            case 2: return Gender::FEMALE;
            case 3: return Gender::OTHER;
            default: 
                throw invalid_argument("invalid gender value " + to_string(value));
        }
    }
};

IDGenerator UserServiceHandler::id_generator;

int main(int argc, char **argv) {
    int port = 9090;
    ::std::shared_ptr<UserServiceHandler> handler(new UserServiceHandler());
    ::std::shared_ptr<TProcessor> processor(new UserServiceProcessor(handler));
    ::std::shared_ptr<TServerTransport> serverTransport(new TServerSocket(port));
    ::std::shared_ptr<TTransportFactory> transportFactory(new TBufferedTransportFactory());
    ::std::shared_ptr<TProtocolFactory> protocolFactory(new TBinaryProtocolFactory());

    TSimpleServer server(processor, serverTransport, transportFactory, protocolFactory);
    server.serve();
    return 0;
}