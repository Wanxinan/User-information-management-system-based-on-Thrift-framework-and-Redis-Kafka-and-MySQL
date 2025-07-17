#include <librdkafka/rdkafkacpp.h>
#include <jsoncpp/json/json.h>
#include <iostream>
#include <vector>

using namespace std;

class UserDeliveryReportCb
:public RdKafka::DeliveryReportCb{

public:
    void dr_cb (RdKafka::Message &message){
        if(message.err()){
            cerr << "Message delivery failed." << endl;
        }
    }
};

int main(){

    //创建配置对象
    string errstr;
    RdKafka::Conf *conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
    
    //设置配置参数
    if(conf->set("bootstrap.servers","localhost:9092",errstr)!=RdKafka::Conf::CONF_OK){
        cerr<<"配置错误: "<<errstr<<endl;
        return 1;
    }

    if(conf->set("group.id","user_consumer",errstr)!=RdKafka::Conf::CONF_OK){
        cerr<<"配置错误: "<<errstr<<endl;
        return 1;
    }

    if(conf->set("auto.offset.reset", "earliest", errstr)!=RdKafka::Conf::CONF_OK){
        cerr<<"配置错误: "<<errstr<<endl;
        return 1;
    }

    // if(conf->set("enable.auto.commit", "true", errstr)!=RdKafka::Conf::CONF_OK){
    //     cerr<<"配置错误: "<<errstr<<endl;
    //     return 1;
    // }
    
    //设置事件回调
    UserDeliveryReportCb user_dr_cb;
    if(conf->set("dr_cb",&user_dr_cb,errstr)!=RdKafka::Conf::CONF_OK){
        cerr<<"设置事件回调失败： "<<errstr<<endl;
        return 1;
    }

    //创建消费者实例
    RdKafka::KafkaConsumer *consumer = RdKafka::KafkaConsumer::create(conf,errstr);
    if(!consumer){
        cerr<<"创建消费者失败： "<<errstr<<endl;
        return 1;
    }
    delete conf;

    //订阅topic
    vector<string> topics;
    topics.push_back("user_updates");
    RdKafka::ErrorCode resp  = consumer->subscribe(topics);
    if(resp != RdKafka::ERR_NO_ERROR){
        cerr<<"订阅失败："<<RdKafka::err2str(resp)<<endl;
        return 1;
    }

    //消费消息
    while(true){
        RdKafka::Message *msg = consumer->consume(1000);
        if(msg->err() == RdKafka::ERR_NO_ERROR){
            Json::Value root;
            Json::Reader reader;
            if(reader.parse((char*)msg->payload(),root)){
                cout<<"User Update: "<<root["action"].asString()<<"\n"
                    <<"ID: "<<root["id"].asInt64()<<"\n"
                    <<"Changes:\n"
                    <<"Username: "<<root["name"].asString()<<"\n"
                    <<"Gender: "<<root["gender"].asInt64()<<"\n"
                    <<"Age: "<<root["age"].asInt64()<<"\n"
                    <<"Phone: "<<root["phone"].asString()<<"\n"
                    <<"Email: "<<root["email"].asString()<<"\n"
                    <<"Description: "<<root["description"].asString()<<"\n"
                    <<"--------------------------"<<"\n";
            }
        }
        delete msg;
    }
    delete consumer;
    return 0;
}