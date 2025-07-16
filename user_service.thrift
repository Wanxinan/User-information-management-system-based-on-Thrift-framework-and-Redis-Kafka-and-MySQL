enum Gender{
    UNKNOWN = 0,
    MALE = 1,
    FEMALE = 2,
    OTHER = 3
}

struct User {
    1: required i64 id,
    2: string username,
    3: Gender gender,
    4: i32 age, 
    5: string phone,
    6: string email,
    7: string description,
    // 新增字段掩码
    8: optional map<string, bool> field_mask
}

exception UserException {
    1: i32 errorCode,
    2: string messgae
}

service UserService {
    User createUsers(1:User user) throws (1:UserException e),
    User getUserInfo(1:i64 id) throws (1:UserException e),
    void updateUserInfo(1:User user) throws (1:UserException e)
}