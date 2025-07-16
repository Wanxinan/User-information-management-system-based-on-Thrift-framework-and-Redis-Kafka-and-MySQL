1、编写一个管理用户信息的服务，通过thrift的远程过程调用实现用户信息管理功能
2、用户信息至少包括 唯一ID、用户名、性别、年龄、手机号、邮箱地址、个人描述
3、提供创建用户、查询用户信息、修改用户信息接口，其中修改用户信息要求除用户ID外其它信息可单独修改
4、数据存储要求使用mysql落地数据（使用存储过程）、使用redis做数据缓存、数据发生变化时通过kafka发送变化消息（使用json数据格式）
5、实现用户信息操作客户端（通过命令行操作）
6、实现用户信息变化处理服务，用户信息变化时输出变化的内容
#编译服务端
g++ -std=c++11 -I/usr/local/include -Igen-cpp -L/usr/local/lib -L/usr/lib64/mysql 
-o server server.cpp gen-cpp/UserService.cpp gen-cpp/user_service_types.cpp 
-lthrift -lhiredis -lrdkafka++ -lrdkafka -ljsoncpp -lmysqlclient

#编译客户端
g++ -std=c++11 -I/usr/local/include -Igen-cpp -L/usr/local/lib -L/usr/lib64/mysql 
-o client client.cpp gen-cpp/UserService.cpp gen-cpp/user_service_types.cpp 
-lthrift -lhiredis -lrdkafka++ -lrdkafka -ljsoncpp -lmysqlclient

#编译消费者
g++ -o consumer consumer.cpp -lrdkafka++ -lrdkafka  -ljsoncpp -lpthread -lz -ldl

./server

./consumer

#创建操作
./client create 1  "wangbiao" male 24 "17766668888" "wangbiao@paipai.com" "C++ Server Engineer" 

#查找操作
./client get 1

#更新操作
./client update 1 --username "libai" --age 99 ...
