
#include<iostream>

int change_a(int &a){
    a = 20;
    return a;
}

int main(){
    char test[] = "Hello";
    // &test[0] == test,因为test[0]为数组的第一个元素，&test[0]为数组的第一个元素的地址，而test为数组的首地址
    // 当std::cout输出一个char数组时，会输出数组的首地址，直到遇到空字符'\0'为止
    std::cout << &test[0] << std::endl;  //输出Hello
    std::cout << test << std::endl;     //输出Hello

    int a = 10;
    if(change_a(a) == 10){
        
    }

    std::cout << "a = " << a << std::endl;

    std::cout << *test << std::endl;
}