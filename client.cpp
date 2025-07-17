#include <iostream>
#include <memory>
#include <vector>
#include <cstring>
#include <algorithm>
#include <string>
#include <cctype>
#include <thrift/transport/TSocket.h> //tcp的传输类
#include <thrift/transport/TBufferTransports.h> //缓冲传输类，提高通信性能
#include <thrift/protocol/TBinaryProtocol.h> //二进制编码解码
#include "gen-cpp/UserService.h"

using namespace apache::thrift;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;
//using namespace user::service;
using namespace std;

struct Command
{
    string name;
    vector<string> args;
};

Command parseCommand(int argc,char* argv[]){
    Command cmd;
    if(argc < 2){
        return cmd;
    }
    
    cmd.name = argv[1];
    for(int i = 2;i < argc;++i){
        cmd.args.push_back(argv[i]);
    }
    return cmd;
}

//打印用户信息
void printUser(const User& user){
    cout<<"User Name: "<<user.username<<"\n"
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

// 帮助信息
void printHelp() {
    std::cout << "User Management Client\n"
              << "Usage:\n"
              << "  create  <username> <gender> <age> <phone> <email> <description>\n"
              << "    gender: male, female, other\n\n"
              << "  get <id>\n\n"
              << "  update <id> [options]\n"
              << "    options:\n"
              << "      --username <name>\n"
              << "      --gender <male|female|other>\n"
              << "      --age <number>\n"
              << "      --phone <number>\n"
              << "      --email <address>\n"
              << "      --description <text>\n";
}


int main(int argc,char** argv){
    std::shared_ptr<TTransport> socket(new TSocket("localhost", 9090)); //创建一个套接字，连接到9090端口
    std::shared_ptr<TTransport> transport(new TBufferedTransport(socket)); //外包一层缓冲传输
    std::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport)); //协议层
    UserServiceClient client(protocol); //客户端对象

    if(argc < 2){
        printHelp();
        return 1;
    }
    
    Command cmd = parseCommand(argc,argv);

    try {
        transport->open();

        if (cmd.name == "create" && cmd.args.size() == 6) {
            User user;
            //user.id = stoll(cmd.args[0]);
            user.username = cmd.args[0];
            
            string gender = cmd.args[1];
            transform(gender.begin(), gender.end(), gender.begin(), ::tolower);
            if (gender == "male") 
                user.gender = Gender::MALE;
            else if (gender == "female") 
                user.gender = Gender::FEMALE;
            else 
                user.gender = Gender::OTHER;
            
            user.age = stoi(cmd.args[2]);
            user.phone = cmd.args[3];
            user.email = cmd.args[4];
            user.description = cmd.args[5];
            
            User created;
            client.createUsers(created, user);
            cout << "User created successfully!\n";
            cout<<"User ID: "<<created.id<<endl;
            printUser(created);
            
        } else if (cmd.name == "get" && cmd.args.size() == 1) {
            User user;
            user.id = stoll(cmd.args[0]);
            client.getUserInfo(user, stoll(cmd.args[0]));
            cout << "User get successfully!\n";
            cout<<"User ID: "<<user.id<<endl;
            printUser(user);
            
        } else if (cmd.name == "update" && cmd.args.size() >= 1) {
            User user;
            user.id = stoll(cmd.args[0]);
            user.__isset.id = true;//关键
            if (user.id == 0) {
                cerr << "Error: id must be provided for update." << endl;
                return 1;
            }
    
            // 创建字段掩码
            map<string, bool> field_mask;
    
            for (size_t i = 1; i < cmd.args.size(); i++) {
                if (cmd.args[i] == "--username" && i + 1 < cmd.args.size()) {
                    user.username = cmd.args[++i];
                    field_mask["username"] = true;
                }
                else if (cmd.args[i] == "--gender" && i + 1 < cmd.args.size()) {
                    string gender = cmd.args[++i];
                    transform(gender.begin(), gender.end(), gender.begin(), ::tolower);
                    
                    if (gender == "male") 
                        user.gender = Gender::MALE;
                    else if (gender == "female") 
                        user.gender = Gender::FEMALE;
                    else 
                        user.gender = Gender::OTHER;
                    
                    field_mask["gender"] = true;
                }
                else if (cmd.args[i] == "--age" && i + 1 < cmd.args.size()) {
                    user.age = stoi(cmd.args[++i]);
                    field_mask["age"] = true;
                }
                else if (cmd.args[i] == "--phone" && i + 1 < cmd.args.size()) {
                    user.phone = cmd.args[++i];
                    field_mask["phone"] = true;
                }
                else if (cmd.args[i] == "--email" && i + 1 < cmd.args.size()) {
                    user.email = cmd.args[++i];
                    field_mask["email"] = true;
                }
                else if (cmd.args[i] == "--description" && i + 1 < cmd.args.size()) {
                    user.description = cmd.args[++i];
                    field_mask["description"] = true;
                }       
            }
    
            // 设置字段掩码
            user.__set_field_mask(field_mask);
            client.updateUserInfo(user);
            
            // 获取并显示更新后的用户信息
            User updated;
            client.getUserInfo(updated, user.id);
            cout << "User update successfully!\n";
            cout<<"User Id:"<<user.id<<endl;
            printUser(updated);
            
        } else {
            printHelp();
        }

        transport->close();
    
    }
    catch (const UserException& ex){
        std::cerr << "UserException: [" << ex.errorCode <<"] "<<ex.message<< std::endl;
        return 1;
    }
    catch (const exception& ex) {
        std::cerr << "ERROR: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}