#include <cmath>
#include <iostream>
#include <fstream>
#include <string>
#include <stdlib.h>
#include <vector>
#include <sstream>
#include <random>
#include "util.h"
#include "Param.h"
using namespace std;

extern Param *param;

std::vector<std::vector<int>> generateRandomMatrix(int rows, int cols) {
    std::random_device rd;  // 用于获取随机种子
    std::mt19937 gen(rd()); // 标准 mersenne_twister_engine
    std::uniform_int_distribution<> dis(0, 1); // 生成0或1的均匀分布

    std::vector<std::vector<int>> matrix(rows, std::vector<int>(cols));

    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            matrix[i][j] = dis(gen); // 生成随机0或1
        }
    }

    return matrix;
}

std::vector<std::vector<double>> generateRandomWeightMatrix(int rows, int cols){
	vector<vector<int>> bitMatrix = generateRandomMatrix(rows,cols);
	vector<vector<double>> weightMatrix(rows,vector<double>(cols));
	for(int i =0; i<bitMatrix.size();i++){
		for(int j =0; j<bitMatrix[0].size();j++){
			if(bitMatrix[i][j] == 1){
				weightMatrix[i][j] = param->maxConductance;
			}
			else{
				weightMatrix[i][j] = param->minConductance;
			}
		}
	}
	return weightMatrix;
}

std::vector<std::vector<double>> generateOnesMatrix(int rows, int cols) {
    // 初始化一个大小为 rows x cols 的矩阵，所有元素为1
    std::vector<std::vector<double>> matrix(rows, std::vector<double>(cols, 1));
    return matrix;
}