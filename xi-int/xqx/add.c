#include <stdio.h>


int adds32(int x, int y)
{
	// CHECK: @trap.sadd.i32(i32 %x, i32 %y)
	// CHECK: add nsw i32 %x, %y
	return x + y;
}

unsigned long long addu64(unsigned long long x, unsigned long long y)
{
	// CHECK: @trap.uadd.i64(i64 %x, i64 %y)
	// CHECK: add i64 %x, %y
	return x + y;
}
/*
extern int n;
int adds32_1(int x, int y)
{
	// CHECK: @trap.sadd.i32(i32 %x, i32 %y)
	// CHECK: add nsw i32 %x, %y
	x = 2;
//	y = 2;
	if(y>4096) return 0;
	int z = x + y;
       // y = x * n;
	if(y > z) return 0;
	printf("%d",z);
	return n;
}

int adds32_2(int x, int y)
{
	// CHECK: @trap.sadd.i32(i32 %x, i32 %y)
	// CHECK: add nsw i32 %x, %y
	x = 2;
//	y = 2;
	int z = x + y;
        y = x * n;
//	if(x > z) return 0;
	printf("%d",z);
	return n;
}
*/
