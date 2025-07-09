#include <iostream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <memory>
#include <stdexcept>
#include <cassert>
#include <queue>
#include <set>
#include <algorithm>


enum class CacheType { KEY, VALUE };

template <typename... Args>
class MutableTuple {
private:
    std::tuple<Args...> data;

public:
    MutableTuple(Args... args) : data(std::make_tuple(args...)) {}

    template <size_t Index>
    auto& get() const{ return std::get<Index>(data); }

    template <size_t Index, typename T>
    void set(T&& value) { std::get<Index>(data) = std::forward<T>(value); }
};


using AG_mapping = std::unordered_map<
        std::string,
        std::unordered_map<
            int,
            std::unordered_map<
                int,
                std::unordered_map<
                    CacheType,
                    std::vector<std::tuple<int, int, int, int>>
                >
            >
        >
    >; //索引参数依次为 req_id head_id decoder_id cache_type
        //索引结果依次为subarray_id l_input （切片长度） start_row/start_col end_row/end_col （subarray上实际的存储位置）

using PE_storage_info = std::unordered_map<
        std::string,
        std::unordered_map<
            int,
            std::unordered_map<
                int,
                std::unordered_map<
                    CacheType,
                    std::vector<MutableTuple<int,int,int>>
                >
            >
        >
    >; //索引参数依次为 req_id head_id decoder_id cache_type
        //索引结果为AG_id, l_start, l_end （该切片于原始decoder的输入位置）


using Tile_storage_info = std::unordered_map<
        std::string,
        std::unordered_map<
            int,
            std::unordered_map<
                int,
                std::unordered_map<
                    CacheType,
                    std::vector<int>
                >
            >
        >
    >; //索引参数依次为 req_id head_id decoder_id cache_type
        //索引结果为存储该请求的PE id

using Channel_storage_info = std::unordered_map<
        std::string,
        std::unordered_map<
            int,
            std::unordered_map<
                CacheType,
                std::vector<int>
            >
        >
    >; //索引参数依次为 req_id head_id cache_type
        //索引结果为存储该请求的Tile id

// 枚举类型区分Key和Value缓存


// Subarray层级 - 实际存储单元
class Subarray {
public:
    Subarray(int id) : id_(id) {
        numRowSubarray = 1024;
        numColSubarray = 1024;
        mem_abl_ = numRowSubarray * numColSubarray *2 ; // 假设每个Subarray初始可用空间为128KB 目前均以2个subarray为一组
        mem_opy_ = 0; // 初始已存储空间为0
    }

    bool operator<(const Subarray& other) const {
        return mem_abl_ < other.mem_abl_;
    }
    
    int id() const { return id_; }

    int mem_abl() const { return mem_abl_; }

    int mem_opy() const { return mem_opy_; }    

    void update_K(){
        mem_abl_ -= numColSubarray*2; //每次增加一行，即1024 * 2
        mem_opy_ += numColSubarray*2; //每次增加一行，即1024 * 2
    }

    void allocate(int size) {
        if (size > mem_abl_) {
            throw std::runtime_error("Not enough memory available in subarray.");
        }
        mem_abl_ -= size;
        mem_opy_ += size;
    }

    void release(int size) {
        if (size > mem_opy_) {
            throw std::runtime_error("Cannot release more memory than currently occupied.");
        }
        mem_abl_ += size;
        mem_opy_ -= size;
    }
    
private:
    int id_;
    int numRowSubarray;
    int numColSubarray;
    int mem_abl_;   //当前未被预留的空间
    int mem_opy_;   //当前已经实际存储的空间

    //若需要在同一个subarray上存储多组数据，需要存储当前的映射位置
};

// ArrayGroup层级 - 存储 head 的 decoder 的切片，切片方式为按照input进行切分
class ArrayGroup {
public:
    ArrayGroup(int id, int num_subarrays) : id_(id), num_subarrays_(num_subarrays){
        for(int i =0;i<num_subarrays_;i++){
            subarrays_.emplace_back(std::make_unique<Subarray>(i));
            subarrays_queue_.insert({subarrays_.back()->mem_abl(), i}); // 使用multiset来维护可用的Subarray，按可用内存大小排序
        }

        //初始化存储情况
        for(int i =0;i<num_subarrays_;i++){
            mem_abl_+=subarrays_[i]->mem_abl();
        }
        mem_opy_ = 0;
    }
    
    int id() const { return id_; }
    
    // 添加映射关系
    void load_KV(
        const std::string& request_id,
        int head_id,
        int decoder_layer,
        CacheType cache_type,
        int l_input,
        int d_head
    ){
        if(cache_type==CacheType::KEY){
            if(d_head!=128){
                throw std::invalid_argument("Key cache type requires d_head to be 128.");
            }
            else{
                int K_len = l_input;
                if(K_len > getMaxSubarray().first){
                    throw std::runtime_error("Not enough memory available in subarray for Key cache.");
                }
                else{
                    // 分配内存
                    int subarray_id = allocateSubarray(K_len*d_head);
                    // 添加映射
                    add_mapping(request_id, head_id, cache_type, decoder_layer, 
                                subarray_id, l_input, 0, K_len-1);
                }
            }
        }
        else{
            throw std::invalid_argument("Unsupported cache type for load_KV.");
        }
    }

    void update_KV(
        const std::string& request_id,
        int head_id,
        int decoder_layer,
        CacheType cache_type,   //只有末尾会更新KV缓存，目前的存储方式不会将同一个decoder拆分后存储在同一个AG里面
        int d_head
    ){
        if(cache_type == CacheType::KEY){
            auto target_subarrays = mappings_[request_id][head_id][decoder_layer][cache_type];
            if (target_subarrays.empty()) {
                throw std::runtime_error("No mapping found for the given request_id, head_id, decoder_layer, and cache_type.");
            }
            else{
                for( const auto& subarray_info:target_subarrays){
                    int subarray_id = std::get<0>(subarray_info);
                    updateSubarray(subarray_id); // 更新Subarray的可用内存大小
                    //TODO 还需要更新subarray的映射信息，但是暂时没有实现
                }
            }
        }
        else{
            throw std::invalid_argument("Unsupported cache type for update_KV.");
        }

    }

    void add_mapping(
        const std::string& request_id,
        int head_id,
        CacheType cache_type,
        int decoder_layer,
        int subarray_id,
        int l_input,
        int start_row,
        int end_row
    ) {
        mappings_[request_id][head_id][decoder_layer][cache_type].emplace_back(
            subarray_id, l_input ,start_row, end_row
        );
    }
    
    // 获取映射信息
    const auto& mappings() const { return mappings_; }

    void print_mappings() const {
        for (const auto& [req_id, heads] : mappings_) {
            for (const auto& [head_id, decoders] : heads) {
                for (const auto& [decoder_layer, cache_types] : decoders) {
                    for (const auto& [cache_type, subarrays] : cache_types) {
                        std::cout << "Request: " << req_id << ", Head: " << head_id 
                                  << ", Decoder Layer: " << decoder_layer 
                                  << ", Cache Type: " << (cache_type == CacheType::KEY ? "KEY" : "VALUE") 
                                  << ", Subarrays: ";
                        for (const auto& subarray_info : subarrays) {
                            std::cout << std::get<0>(subarray_info) << " ";
                        }
                        std::cout << std::endl;
                    }
                }
            }
        }
    }
    
    // 删除指定请求的映射
    void remove_request(const std::string& request_id) {
        mappings_.erase(request_id);
    }
    
    // 添加Subarray
    void add_subarray(std::unique_ptr<Subarray> subarray) {
        subarrays_.push_back(std::move(subarray));
    }
    
    const auto& subarrays() const { return subarrays_; }

    int mem_abl() const{
        return mem_abl_;
    }

    int mem_opy() const{
        return mem_opy_;
    }
    
private:
    int id_;
    int num_subarrays_;
    int mem_abl_;
    int mem_opy_;
    std::vector<std::unique_ptr<Subarray>> subarrays_;
    // std::priority_queue<std::unique_ptr<Subarray>> subarrays_queue_;
    std::multiset<std::pair<int,int>,std::greater<>> subarrays_queue_; // 使用multiset来维护可用的Subarray，按可用内存大小排序
    // 映射结构: request_id -> head_id -> cache_type -> decoder_layer -> (subarray_id, start_row, end_row)
    AG_mapping mappings_;

    int allocateSubarray(int size) {
        // 分配一个Subarray的内存
        if (subarrays_queue_.empty()) {
            throw std::runtime_error("No available subarrays.");
        }
        
        auto it = subarrays_queue_.begin();
        if (it->first < size) {
            throw std::runtime_error("Not enough memory available in subarray.");
        }
        
        int index = it->second;
        subarrays_[index]->allocate(size);
        mem_abl_ -=size;
        mem_opy_ +=size;
        
        // 更新优先队列
        subarrays_queue_.erase(it);
        subarrays_queue_.insert({subarrays_[index]->mem_abl(), index});
        return index;
    }

    void updateSubarray(int index) {
        // 更新指定索引的Subarray的可用内存大小 目前暂时不考虑一个subarray上存储多个请求的情况，因此直接简单地增加一行即可
        //TODO 暂未将abl和opy区分开，没有考虑预留内存和实际内存
        subarrays_queue_.erase({subarrays_[index]->mem_abl(), index});
        
        subarrays_[index]->update_K(); // 更新Subarray的可用内存和已存储内存
        
        subarrays_queue_.insert({subarrays_[index]->mem_abl(), index});
        
    }

    std::pair<int,int>getMaxSubarray() {
        // 获取当前可用内存最大的Subarray
        if (subarrays_queue_.empty()) {
            throw std::runtime_error("No available subarrays.");
        }
        return *subarrays_queue_.begin();
    }
};

// PE层级 - 存储decoder层信息
class PE {
public:
    PE(int id, int num_AGs=4, int num_subarrays=64) : id_(id),num_AGs_(num_AGs), num_subarrays_(num_subarrays) {
        // 初始化PE时可以添加一些默认的ArrayGroup
        for (int i = 0; i < num_AGs_; ++i) { // 假设每个PE有4个ArrayGroup
            array_groups_.emplace_back(std::make_unique<ArrayGroup>(i, num_subarrays_)); // 每个ArrayGroup有16个Subarray
        }

        for( int i =0;i<num_AGs_;i++){
            mem_abl_+=array_groups_[i]->mem_abl();
        }
        mem_opy_ = 0;
        
    }
    
    void load_KV(
        const std::string& request_id,
        int head_id,
        int decoder_layer,
        CacheType cache_type,
        int l_input,
        int d_head
    ) {
        if (cache_type == CacheType::KEY) {
            if (d_head != 128) {
                throw std::invalid_argument("Key cache type requires d_head to be 128.");
            } else {
                //按照输入长度1024为单位进行切分
                int i = 0;
                while (l_input > 0) {
                    int K_len = std::min(l_input, 1024); // 每次处理1024个输入
                    l_input -= K_len;
                    int l_start = i * 1024;
                    int l_end = l_start + K_len - 1;
                    

                    // 寻找合适的ArrayGroup并分配内存
                    int AG_id = find_AG(request_id,head_id,decoder_layer,cache_type,l_input,d_head);

                    

                    // 更新PE存储信息
                    add_storage_info(request_id, head_id, decoder_layer, cache_type,AG_id,l_start,l_end);
                    
                    i++;
                }
            }
        } else {
            throw std::invalid_argument("Unsupported cache type for loadKV.");
        }
    }

    int id() const { return id_; }

    int mem_abl() const{ return mem_abl_;}

    int mem_opy() const{ return mem_opy_;}
    
    // 添加存储信息
    void add_storage_info(
        const std::string& request_id,
        int head_id,
        int decoder_layer,
        CacheType cache_type,
        int AG_id,
        int l_start,
        int l_end
    ) {
        storage_info_[request_id][head_id][decoder_layer][cache_type].push_back(MutableTuple<int,int,int>(AG_id,l_start,l_end));
    }
    
    // 获取存储信息
    const auto& storage_info() const { return storage_info_; }
    
    void print_storage_info() const {
        for (const auto& [req_id, heads] : storage_info_) {
            for (const auto& [head_id, decoders] : heads) {
                for (const auto& [decoder_layer, cache_types] : decoders) {
                    for (const auto& [cache_type, AGs] : cache_types) {
                        std::cout << "Request: " << req_id << ", Head: " << head_id 
                                  << ", Decoder Layer: " << decoder_layer 
                                  << ", Cache Type: " << (cache_type == CacheType::KEY ? "KEY" : "VALUE") 
                                  << ", AGs: "<<std::endl;

                        std::cout<<"AG_id"<<" "<<"l_start"<<" "<<"l_end"<<std::endl;
                        for (int i=0;i<AGs.size();i++) {
                            std::cout << AGs[i].get<0>() << " "<<AGs[i].get<1>() << " "<<AGs[i].get<2>()<<std::endl;
                        }
                        std::cout << std::endl;
                    }
                }
            }
        }
    }

    // 删除指定请求的信息
    void remove_request(const std::string& request_id) {
        storage_info_.erase(request_id);
        for (auto& group : array_groups_) {
            group->remove_request(request_id);
        }
    }
    
    // 添加ArrayGroup
    void add_array_group(std::unique_ptr<ArrayGroup> group) {
        array_groups_.push_back(std::move(group));
    }
    
    const auto& array_groups() const { return array_groups_; }
    
private:
    int id_;
    int num_AGs_;
    int num_subarrays_;
    int mem_abl_;
    int mem_opy_;
    std::vector<std::unique_ptr<ArrayGroup>> array_groups_;
    
    // 存储结构: request_id -> head_id -> cache_type -> set of decoder_layers
    PE_storage_info storage_info_;

    int find_AG(
        const std::string& request_id,
        int head_id,
        int decoder_layer,
        CacheType cache_type,
        int l_input,
        int d_head
    ){
        //从当前的4个AG中选择一个进行返回，分配规则
        //1、容量满足
        //2、不能将同一个decoder_layer的存在一起
        //3、优先考虑将同一个head的存在一起
        //4、优先考虑将同一个decoder_layer的KV存在一起

        int size = l_input*d_head*16; //注意力头切片的大小

        std::vector<int>candidates(num_AGs_);

        for(int i =0;i<num_AGs_;i++){
            if(array_groups_[i]->mem_abl()>=size) candidates.push_back(i);
        }

        int AG_id = 0;

        if(candidates.empty()) throw std::runtime_error("No available subarrays.");
        else{
            AG_id =  candidates[0];//TODO 这里需要实现一个复杂的分配逻辑，但是可能需要简化。
        }

        
        array_groups_[AG_id]->load_KV(request_id,head_id,decoder_layer,cache_type,l_input,d_head); //TODO 这里可以修改AG的逻辑，AG不需要存储l_start 和 l_end 信息

        mem_abl_-=size;
        mem_opy_+=size;
        return AG_id;
        
    }

    // void allocate_AG(
    //     const std::string& request_id,
    //     int head_id,
    //     int decoder_layer,
    //     CacheType cache_type,
    //     int l_start,
    //     int l_end,
    //     int d_head
    // ){
    //     array_groups_[AG_id]->load_KV(request_id,head_id,)
    // }
};

// Tile层级 - 存储PE信息
class Tile {
public:
    Tile(int id,int num_pes=8, int num_AGs=4, int num_subarrays=64 ) : id_(id),num_pes_(num_pes), num_AGs_(num_AGs), num_subarrays_(num_subarrays) {
        // 初始化Tile时可以添加一些默认的PE
        for (int i = 0; i < num_pes; ++i) { // 假设每个Tile有8个PE
            pes_.emplace_back(std::make_unique<PE>(i, num_AGs, num_subarrays)); // 每个PE有4个ArrayGroup，每个ArrayGroup有16个Subarray
        }

        // 初始化Tile的可用内存和已存储内存
        mem_abl_ = 0;
        mem_opy_ = 0;
        for (const auto& pe : pes_) {
            mem_abl_ += pe->mem_abl();
        }

    }
    
    int id() const { return id_; }

    int mem_abl() const{ return mem_abl_;}
    
    int mem_opy() const{ return mem_opy_;}

    void load_KV(
        const std::string& request_id,
        int head_id,
        int decoder_layer,
        CacheType cache_type,
        int l_input,
        int d_head
    ){
        if(cache_type == CacheType::KEY){
            if (d_head != 128) {
                throw std::invalid_argument("Key cache type requires d_head to be 128.");
            } else {
                // 寻找合适的PE并分配内存
                int pe_id = allocate_pe(request_id, head_id, decoder_layer, cache_type, l_input, d_head);
                
                // 更新Tile存储信息
                add_storage_info(request_id, head_id, decoder_layer , cache_type, pe_id);
            }
        }
        else{
            throw std::invalid_argument("Unsupported cache type for load_KV.");
        }
    }
    
    // 添加存储信息
    void add_storage_info(
        const std::string& request_id,
        int head_id,
        int decoder_layer,
        CacheType cache_type,
        int pe_id
    ) {
        storage_info_[request_id][head_id][decoder_layer][cache_type].push_back(pe_id);
    }

    void print_storage_info() const {
        for (const auto& [req_id, heads] : storage_info_) {
            for (const auto& [head_id, decoders] : heads) {
                for (const auto& [decoder_layer, cache_types] : decoders) {
                    for (const auto& [cache_type, pes] : cache_types) {
                        std::cout << "Request: " << req_id << ", Head: " << head_id 
                                  << ", Decoder Layer: " << decoder_layer 
                                  << ", Cache Type: " << (cache_type == CacheType::KEY ? "KEY" : "VALUE") 
                                  << ", PEs: ";

                        for (int i=0;i<pes.size();i++) {
                            std::cout << pes[i] << " "; // 输出存储该请求的PE ID
                        }
                        std::cout << std::endl;
                    }
                }
            }
        }
    }
    
    // 获取存储信息
    const auto& storage_info() const { return storage_info_; }
    
    // 删除指定请求的信息
    void remove_request(const std::string& request_id) {
        storage_info_.erase(request_id);
        for (auto& pe : pes_) {
            pe->remove_request(request_id);
        }
    }
    
    // 添加PE
    void add_pe(std::unique_ptr<PE> pe) {
        pes_.push_back(std::move(pe));
    }
    
    const auto& pes() const { return pes_; }
    
private:
    int id_;
    int num_pes_;
    int num_AGs_;
    int num_subarrays_;
    // 当前Tile的可用内存和已存储内存
    int mem_abl_;
    int mem_opy_;   
    std::vector<std::unique_ptr<PE>> pes_;
    
    // 存储结构: request_id -> head_id -> cache_type -> set of PE IDs
    Tile_storage_info storage_info_;

    int allocate_pe(
        const std::string& request_id,
        int head_id,
        int decoder_layer,
        CacheType cache_type,
        int l_input,
        int d_head
    ) {
        int pe_id = 0; // 假设只使用第一个PE进行示例

        if (pes_.empty()) {
            throw std::runtime_error("No available PEs in Tile.");
        }
        
        pes_[0]->load_KV(request_id, head_id, decoder_layer, cache_type, l_input, d_head);
        
        return pe_id;
    }
    
};

// MemoryDie层级 - 顶层存储管理
class Channel {
public:
    Channel(int id, int num_tiles,int num_pes=8, int num_AGs=4, int num_subarrays=64 ) :id_(id), num_tiles_(num_tiles), num_pes_(num_pes),num_AGs_(num_AGs), num_subarrays_(num_subarrays) 
        // 初始化Tile层级结构
    {
        for( int tile_id = 0; tile_id < num_tiles_; ++tile_id) {
            tiles_.emplace_back(std::make_unique<Tile>(tile_id, num_pes_, num_AGs_, num_subarrays_));
        }
        // 初始化Tile的KV信息
        mem_abl_ = 0;
        mem_opy_ = 0;
        for (const auto& tile : tiles_) {
            mem_abl_ += tile->mem_abl();    
        }
    }
    
    // 添加KV映射
    void add_kv_mapping(
        const std::string& request_id,
        int head_id,
        CacheType cache_type,
        int decoder_layer,
        int tile_id,
        int pe_id,
        int group_id,
        int subarray_id,
        int start_row,
        int end_row
    ) {
        tile_kv_info_[request_id][head_id][cache_type].push_back(tile_id);
    }
    
    // 获取Tile的KV信息
    const auto& tile_kv_info() const {
        return tile_kv_info_;
    }
    
    // 获取Tile的存储详情
    const auto& tile_storage_info(int tile_id) const {
        static const Tile_storage_info empty;
        if (tile_id < 0 || tile_id >= num_tiles_) return empty;
        return tiles_[tile_id]->storage_info();
    }
    
    // 获取PE的存储详情
    const auto& pe_storage_info(int tile_id, int pe_id) const {
        static const PE_storage_info empty;
        if (tile_id < 0 || tile_id >= num_tiles_) return empty;
        if (pe_id < 0 || pe_id >= num_pes_) return empty;
        return tiles_[tile_id]->pes()[pe_id]->storage_info();
    }
    
    // 获取ArrayGroup的映射详情
    const auto& array_group_mapping(int tile_id, int pe_id, int group_id) const {
        static const AG_mapping empty;
        if (tile_id < 0 || tile_id >= num_tiles_) return empty;
        if (pe_id < 0 || pe_id >= num_pes_) return empty;
        if (group_id < 0 || group_id >= num_AGs_) return empty;
        return tiles_[tile_id]->pes()[pe_id]->array_groups()[group_id]->mappings();
    }
    
    // 删除整个请求
    void remove_request(const std::string& request_id) {
        
        tile_kv_info_.erase(request_id);
        for (auto& tile : tiles_) {
            tile->remove_request(request_id);
        }
    }

    void print_tile_kv_info() const {
        for (const auto& [req_id, heads] : tile_kv_info_) {
            for (const auto& [head_id, cache_types] : heads) {
                for (const auto& [cache_type, tiles] : cache_types) {
                    std::cout << "Request: " << req_id << ", Head: " << head_id 
                              << ", Cache Type: " << cache_type_str(cache_type) 
                              << ", Tiles: ";
                    for (const auto& tile_id : tiles) {
                        std::cout << tile_id << " ";
                    }
                    std::cout << std::endl;
                }
            }
        }
    }
    
private:
    int num_tiles_;
    int num_pes_;
    int num_AGs_;
    int num_subarrays_;
    
    int id_;
    int mem_abl_;
    int mem_opy_;

    std::vector<std::unique_ptr<Tile>> tiles_;
    
    // Tile存储信息: tile_id -> request_id -> set of head_ids
    Channel_storage_info tile_kv_info_;
    
};

// 辅助函数：打印CacheType
std::string cache_type_str(CacheType type) {
    return (type == CacheType::KEY) ? "KEY" : "VALUE";
}

// 测试用例
int main() {
    // 配置硬件参数
    const int NUM_TILES = 4;
    const int PES_PER_TILE = 8;
    const int GROUPS_PER_PE = 4;
    const int SUBARRAYS_PER_GROUP = 16;
    
    // 创建MemoryDie
    Channel die(NUM_TILES, PES_PER_TILE, GROUPS_PER_PE, SUBARRAYS_PER_GROUP);
    
    // 添加KV映射
    die.add_kv_mapping("req1", 3, CacheType::KEY, 2, 0, 1, 2, 5, 0, 127);
    die.add_kv_mapping("req1", 3, CacheType::VALUE, 2, 0, 1, 2, 6, 0, 127);
    die.add_kv_mapping("req2", 5, CacheType::KEY, 1, 2, 3, 1, 3, 64, 191);
    
    // 查询各层级信息

    

    

    

    
    // 删除请求
    die.remove_request("req1");
    
    std::cout << std::endl;
    
    return 0;
}