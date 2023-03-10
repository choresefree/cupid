/**
* @Projectname: cupid
* @Author: sefree
* @Email: 1836662622@qq.com
* @Data:2023/2/3 01:18
* @Description: TODO
*/
# include "common/common.h"

bool dump_file(const std::string& content, const std::string& file_path){
    std::fstream ifs;
    ifs.open(file_path, std::ios::out);
    ifs << content;
    return true;
}


bool dump_json(const Json::Value& json, const std::string& file_path) {
    std::string str_json = json.toStyledString();
    dump_file(str_json, file_path);
    return true;
}

bool load_json(Json::Value& json, const std::string& file_path){
    std::fstream ifs;
    ifs.open(file_path, std::ios::in);
    if (!ifs.is_open())
    {
        printf("fail to open file\n");
        return false;
    }
    std::string content;
    std::string buf;
    while(getline(ifs,buf))          //与输入一行字符串类似getline(cin,s);
    {
        content += buf;
    }
    ifs.close();
    Json::Reader reader;
    if (reader.parse(content, json)) {
        return true;
    }
    return false;
}