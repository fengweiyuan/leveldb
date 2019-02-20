#include <assert.h>
#include <iostream>
#include "leveldb/db.h"

using namespace std;

int main(){
    leveldb::DB* db;
    leveldb::Options options;
    options.create_if_missing = true;
    // 打开数据库
    leveldb::Status status = leveldb::DB::Open(options,"./testdb",&db);

    std::string key = "fwy";
    std::string value = "myValue1";
    std::string result;

    // 添加
    status = db->Put(leveldb::WriteOptions(), key, value);
    assert(status.ok());

    // 读取
    status = db->Get(leveldb::ReadOptions(), key, &result);
    assert(status.ok());
    std::cout << result << std::endl;

    // 修改（重新赋值）
    std::string value2 = "myValue2";
    status = db->Put(leveldb::WriteOptions(), key, value2);
    assert(status.ok());
    status = db->Get(leveldb::ReadOptions(), key, &result);
    cout<< result <<endl;

    // 删除
    status = db->Delete(leveldb::WriteOptions(),key);//删除
    assert(status.ok());
    status = db->Get(leveldb::ReadOptions(), key, &value);
    if(!status.ok()){
        std::cerr << status.ToString() << std::endl;
    }else{
        std::cout << status.ToString() <<std::endl;
    }

    delete db; //关闭数据库

    return 0;  
}
