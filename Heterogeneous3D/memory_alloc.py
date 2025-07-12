from typing import Dict, List, Tuple, Set, Optional, Any

class Subarray:
    def __init__(self, subarray_id: int):
        self.id = subarray_id
        # 实际存储的KV数据（可选，根据需求实现）
        self.kv_data: Optional[Any] = None

class ArrayGroup:
    def __init__(self, group_id: int):
        self.id = group_id
        self.subarrays: List[Subarray] = []
        # 存储映射信息：request_id -> head_id -> is_key -> decoder_layer -> List[(subarray_id, start_row, end_row)]
        self.mapping: Dict[str, Dict[int, Dict[bool, Dict[int, List[Tuple[int, int, int]]]]]] = {}
    
    def add_mapping(
        self,
        request_id: str,
        head_id: int,
        is_key: bool,
        decoder_layer: int,
        subarray_id: int,
        start_row: int,
        end_row: int
    ):
        """添加KV缓存映射关系"""
        if request_id not in self.mapping:
            self.mapping[request_id] = {}
        if head_id not in self.mapping[request_id]:
            self.mapping[request_id][head_id] = {True: {}, False: {}}
        if decoder_layer not in self.mapping[request_id][head_id][is_key]:
            self.mapping[request_id][head_id][is_key][decoder_layer] = []
        
        self.mapping[request_id][head_id][is_key][decoder_layer].append(
            (subarray_id, start_row, end_row)
        )
    
    def get_mapping(self) -> Dict[str, Dict[int, Dict[bool, Dict[int, List[Tuple[int, int, int]]]]]]:
        """获取当前存储的映射关系"""
        return self.mapping

class PE:
    def __init__(self, pe_id: int):
        self.id = pe_id
        self.array_groups: List[ArrayGroup] = []
        # 存储信息：request_id -> head_id -> is_key -> Set[decoder_layer]
        self.storage_info: Dict[str, Dict[int, Dict[bool, Set[int]]]] = {}
    
    def add_storage_info(
        self,
        request_id: str,
        head_id: int,
        is_key: bool,
        decoder_layer: int
    ):
        """添加存储信息"""
        if request_id not in self.storage_info:
            self.storage_info[request_id] = {}
        if head_id not in self.storage_info[request_id]:
            self.storage_info[request_id][head_id] = {True: set(), False: set()}
        
        self.storage_info[request_id][head_id][is_key].add(decoder_layer)
    
    def get_storage_info(self) -> Dict[str, Dict[int, Dict[bool, Set[int]]]]:
        """获取当前存储信息"""
        return self.storage_info

class Tile:
    def __init__(self, tile_id: int):
        self.id = tile_id
        self.pes: List[PE] = []
        # 存储信息：request_id -> head_id -> Dict[is_key, Set[pe_id]]
        self.storage_info: Dict[str, Dict[int, Dict[bool, Set[int]]]] = {}
    
    def add_storage_info(
        self,
        request_id: str,
        head_id: int,
        is_key: bool,
        pe_id: int
    ):
        """添加存储信息"""
        if request_id not in self.storage_info:
            self.storage_info[request_id] = {}
        if head_id not in self.storage_info[request_id]:
            self.storage_info[request_id][head_id] = {True: set(), False: set()}
        
        self.storage_info[request_id][head_id][is_key].add(pe_id)
    
    def get_storage_info(self) -> Dict[str, Dict[int, Dict[bool, Set[int]]]]:
        """获取当前存储信息"""
        return self.storage_info

class MemoryDie:
    def __init__(
        self,
        num_tiles: int,
        num_pes_per_tile: int,
        num_groups_per_pe: int,
        num_subarrays_per_group: int
    ):
        self.tiles: List[Tile] = []
        # 存储信息：tile_id -> Dict[request_id, Set[head_id]]
        self.tile_kv_info: Dict[int, Dict[str, Set[int]]] = {}
        
        # 初始化硬件层级结构
        for tile_id in range(num_tiles):
            tile = Tile(tile_id)
            for pe_id in range(num_pes_per_tile):
                pe = PE(pe_id)
                for group_id in range(num_groups_per_pe):
                    group = ArrayGroup(group_id)
                    for subarray_id in range(num_subarrays_per_group):
                        group.subarrays.append(Subarray(subarray_id))
                    pe.array_groups.append(group)
                tile.pes.append(pe)
            self.tiles.append(tile)
            self.tile_kv_info[tile_id] = {}
    
    def add_kv_mapping(
        self,
        request_id: str,
        head_id: int,
        is_key: bool,
        decoder_layer: int,
        tile_id: int,
        pe_id: int,
        group_id: int,
        subarray_id: int,
        start_row: int,
        end_row: int
    ):
        """添加KV缓存映射关系"""
        # 验证位置有效性
        if tile_id >= len(self.tiles):
            raise ValueError(f"Invalid tile_id: {tile_id}")
        tile = self.tiles[tile_id]
        
        if pe_id >= len(tile.pes):
            raise ValueError(f"Invalid pe_id: {pe_id}")
        pe = tile.pes[pe_id]
        
        if group_id >= len(pe.array_groups):
            raise ValueError(f"Invalid group_id: {group_id}")
        group = pe.array_groups[group_id]
        
        if subarray_id >= len(group.subarrays):
            raise ValueError(f"Invalid subarray_id: {subarray_id}")
        
        # 添加映射关系
        group.add_mapping(
            request_id, head_id, is_key, decoder_layer, 
            subarray_id, start_row, end_row
        )
        
        # 更新PE存储信息
        pe.add_storage_info(request_id, head_id, is_key, decoder_layer)
        
        # 更新Tile存储信息
        tile.add_storage_info(request_id, head_id, is_key, pe_id)
        
        # 更新Die存储信息
        if request_id not in self.tile_kv_info[tile_id]:
            self.tile_kv_info[tile_id][request_id] = set()
        self.tile_kv_info[tile_id][request_id].add(head_id)
    
    def get_tile_kv_info(self, tile_id: int) -> Dict[str, Set[int]]:
        """获取指定Tile中存储的request和head信息"""
        return self.tile_kv_info.get(tile_id, {})
    
    def get_tile_storage_info(self, tile_id: int) -> Dict[str, Dict[int, Dict[bool, Set[int]]]]:
        """获取指定Tile的存储信息"""
        if tile_id < len(self.tiles):
            return self.tiles[tile_id].get_storage_info()
        return {}
    
    def get_pe_storage_info(self, tile_id: int, pe_id: int) -> Dict[str, Dict[int, Dict[bool, Set[int]]]]:
        """获取指定PE的存储信息"""
        if tile_id < len(self.tiles) and pe_id < len(self.tiles[tile_id].pes):
            return self.tiles[tile_id].pes[pe_id].get_storage_info()
        return {}
    
    def get_array_group_mapping(self, tile_id: int, pe_id: int, group_id: int
                               ) -> Dict[str, Dict[int, Dict[bool, Dict[int, List[Tuple[int, int, int]]]]]]:
        """获取指定Array Group的映射信息"""
        if (tile_id < len(self.tiles) and 
            pe_id < len(self.tiles[tile_id].pes) and 
            group_id < len(self.tiles[tile_id].pes[pe_id].array_groups)):
            return self.tiles[tile_id].pes[pe_id].array_groups[group_id].get_mapping()
        return {}
    
    def remove_request(self, request_id: str):
        """删除整个请求的所有KV数据"""
        for tile in self.tiles:
            # 更新Die级存储信息
            if tile.id in self.tile_kv_info and request_id in self.tile_kv_info[tile.id]:
                del self.tile_kv_info[tile.id][request_id]
            
            # 更新Tile级存储信息
            if request_id in tile.storage_info:
                del tile.storage_info[request_id]
            
            for pe in tile.pes:
                # 更新PE级存储信息
                if request_id in pe.storage_info:
                    del pe.storage_info[request_id]
                
                for group in pe.array_groups:
                    # 更新Array Group级映射
                    if request_id in group.mapping:
                        del group.mapping[request_id]

# 使用示例
if __name__ == "__main__":
    # 可配置的硬件参数
    DIE_CONFIG = {
        "num_tiles": 4,
        "num_pes_per_tile": 8,
        "num_groups_per_pe": 4,
        "num_subarrays_per_group": 16
    }
    
    # 初始化Memory Die
    die = MemoryDie(**DIE_CONFIG)
    
    # 添加KV映射
    die.add_kv_mapping(
        request_id="req1",
        head_id=3,
        is_key=True,
        decoder_layer=2,
        tile_id=0,
        pe_id=1,
        group_id=2,
        subarray_id=5,
        start_row=0,
        end_row=127
    )
    
    die.add_kv_mapping(
        request_id="req1",
        head_id=3,
        is_key=False,
        decoder_layer=2,
        tile_id=0,
        pe_id=1,
        group_id=2,
        subarray_id=6,
        start_row=0,
        end_row=127
    )
    
    die.add_kv_mapping(
        request_id="req2",
        head_id=5,
        is_key=True,
        decoder_layer=1,
        tile_id=2,
        pe_id=3,
        group_id=1,
        subarray_id=3,
        start_row=64,
        end_row=191
    )
    
    # 查询各层级信息
    print("Die Level - Tile 0 storage:")
    print(die.get_tile_kv_info(0))  # 输出: {'req1': {3}}
    
    print("\nTile Level - Tile 0 storage details:")
    print(die.get_tile_storage_info(0))  # 输出: {'req1': {3: {True: {1}, False: {1}}}}
    
    print("\nPE Level - Tile0/PE1 storage details:")
    print(die.get_pe_storage_info(0, 1))  # 输出: {'req1': {3: {True: {2}, False: {2}}}}
    
    print("\nArrayGroup Level - Tile0/PE1/Group2 mapping:")
    print(die.get_array_group_mapping(0, 1, 2))
    # 输出: {
    #   'req1': {
    #       3: {
    #           True: {2: [(5, 0, 127)]},
    #           False: {2: [(6, 0, 127)]}
    #       }
    #   }
    # }
    
    # 删除请求
    die.remove_request("req1")
    print("\nAfter removing req1:")
    print("Die Level - Tile 0 storage:", die.get_tile_kv_info(0))  # 输出: {}
    print("PE Level - Tile0/PE1 storage:", die.get_pe_storage_info(0, 1))  # 输出: {}