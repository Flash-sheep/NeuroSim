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

using AG_mapping = std::unordered_map<
        std::string,
        std::unordered_map<
            int,
            std::unordered_map<
                int,
                std::unordered_map<
                    CacheType,
                    std::vector<std::tuple<int, int, int, int, int>>
                >
            >
        >
    >; //索引参数依次为 req_id head_id decoder_id 
        //索引结果依次为subarray_id l_start l_end （该切片于原始decoder的输入位置） start_row/start_col end_row/end_col （subarray上实际的存储位置）

using PE_storage_info = std::unordered_map<
        std::string,
        std::unordered_map<
            int,
            std::unordered_map<
                CacheType,
                std::unordered_set<int>
            >
        >
    >;

using Tile_storage_info = std::unordered_map<
        std::string,
        std::unordered_map<
            int,
            std::unordered_map<
                CacheType,
                std::unordered_set<int>
            >
        >
    >;

using Die_storage_info = std::unordered_map<
        int,
        std::unordered_map<
            std::string,
            std::unordered_set<int>
        >
    >;

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
    }
    
    int id() const { return id_; }
    
    // 添加映射关系
    void load_KV(
        const std::string& request_id,
        int head_id,
        int decoder_layer,
        CacheType cache_type,
        int start_l,
        int end_l,
        int d_head
    ){
        if(cache_type==CacheType::KEY){
            if(d_head!=128){
                throw std::invalid_argument("Key cache type requires d_head to be 128.");
            }
            else{
                int K_len = end_l-start_l;
                if(K_len > getMaxSubarray().first){
                    throw std::runtime_error("Not enough memory available in subarray for Key cache.");
                }
                else{
                    // 分配内存
                    int subarray_id = allocateSubarray(K_len*d_head);
                    // 添加映射
                    add_mapping(request_id, head_id, cache_type, decoder_layer, 
                                subarray_id, start_l, end_l, 0, K_len-1);
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
        int start_l,
        int end_l,
        int start_row,
        int end_row
    ) {
        mappings_[request_id][head_id][decoder_layer][cache_type].emplace_back(
            subarray_id, start_l, end_l ,start_row, end_row
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
    
private:
    int id_;
    int num_subarrays_;
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
        
        // 更新优先队列
        subarrays_queue_.erase(it);
        subarrays_queue_.insert({subarrays_[index]->mem_abl(), index});
        return index;
    }

    void updateSubarray(int index) {
        // 更新指定索引的Subarray的可用内存大小 目前暂时不考虑一个subarray上存储多个请求的情况，因此直接简单地增加一行即可
        
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

int main(){
    // 测试Subarray和ArrayGroup的功能
    ArrayGroup ag(0, 32); // 创建一个ArrayGroup，包含2个Subarray

    try {
        ag.load_KV("req1", 0, 0, CacheType::KEY, 0, 1024, 128);
        std::cout << "Loaded KV successfully." << std::endl;
        
        ag.update_KV("req1", 0, 0, CacheType::KEY, 128);
        std::cout << "Updated KV successfully." << std::endl;

        ag.load_KV("req2", 0, 0, CacheType::KEY, 0, 1024, 128);
        
        ag.print_mappings();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}