- [1 内核级线程模型](#1--------)
  * [1.1 内核级线程概念、图示](#11-----------)
  * [1.2 内核级线程的切换五段论](#12------------)
    + [1.2.1 从一个栈到一套栈](#121---------)
    + [1.2.2 第一阶段-中断进入阶段](#122------------)
    + [1.2.3 第二阶段-调用schedule](#123--------schedule)
    + [1.2.4 第三阶段-内核栈的切换](#124------------)
    + [1.2.5 第四阶段-中断返回阶段](#125------------)
    + [1.2.6 第五阶段-用户栈切换](#126-----------)
  * [1.3 内核级线程的创建](#13---------)
  * [1.4 Linux-0.11操作系统中的多进程视图](#14-linux-011-----------)
- [1.4.1 0号进程的创建](#141-0------)
- [1.4.2 1号进程的创建](#142-1------)
- [1.4.3 其他进程的创建](#143--------)
- [2 Linux-0.11中的进程切换](#2-linux-011------)
- [3 基于内核栈完成进程切换](#3------------)
- [3.1 为什么要修改原有的进程切换方式](#31----------------)
- [3.2 要做的内容](#32------)
- [3.3 内核栈切换的图示(精华)](#33-------------)
- [3.3 修改PCB的定义和INIT_STACK宏](#33---pcb----init-stack-)
- [3.4 重写switch_to](#34---switch-to)
- [3.5 修改schedule()函数](#35---schedule----)
- [3.5 修改fork](#35---fork)
- [3.6 编译运行](#36-----)
- [4 归纳总结](#4-----)
  * [4.1 遇到的问题](#41------)
    + [4.1.1 汇编语言怎么调用C语言声明的全局变量?](#411---------c----------)
    + [4.1.2 C语言如何调用汇编语言的函数？](#412-c--------------)
    + [4.1.3. pop和popl到底是怎么工作的？即如果push 32位,pop 16位是怎么协调的？](#413-pop-popl------------push-32--pop-16--------)
  * [4.2 再谈fork](#42---fork)
- [5 代码地址](#5-----)

<small><i><a href='http://ecotrust-canada.github.io/markdown-toc/'>Table of contents generated with markdown-toc</a></i></small>
# 1 内核级线程模型
## 1.1 内核级线程概念、图示
1. 用户级线程的缺点：如果一个用户级线程在内核中阻塞，则这个进程的所有用户级线程将全部阻塞。这就限制了用户级线程的并发程度，从而限制了由并发性带来的计算机硬件工作效率的提升。
2. 内核级线程由更好的并发性。
3. 对于多核CPU而言：
    1.用户级线程只是在一个核上跑
    2.两个进程不适合放在处理器中的多个核上执行，因为多核处理器中的多个核通常要共享存储管理部件(MMU)以及Cache，如果把两个进程放在多个核上，在切换进程时候也要切换MMU和Cahe，和单核处理器没什么两样。
    3.属于同一个进程的内核级线程共享同一地址空间、缓存，非常适合。
    
## 1.2 内核级线程的切换五段论
### 1.2.1 从一个栈到一套栈
栈常被用来实现跳转，比如在C语言用栈帧实现函数的跳转，用户级线程用用户栈实现函数的跳转。内核级线程的跳转也应该用栈来实现。正如每个用户级线程都有一个用户栈一样，每个内核级线程都应该有一个用户栈，用来存储用户函数，但是内核栈要进入操作系统，必须也要有一个内核栈，因为内核中也要实现函数的跳转。
1. 用户栈应该记录的东西:用户代码的执行过程
2. 内核栈应该记录的东西:用户段的PC指针，用户栈的地址
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191128182353531.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
### 1.2.2 第一阶段-中断进入阶段
中断进入就是int指令，或者硬件中断的中断处理入口。比如int 80系统调用中断的中断处理入口为_system_call，比如硬件中断时钟中断int 20的中断处理入口为_timer_interrupter。中断进入阶段的核心工作是记录当前程序在用户态执行的信息。当前程序的执行信息包括这几个部分:
1. 用户栈地址SS:SP
2. 标志寄存器EFLAGS
3. PC指针CS:IP
4. 程序执行现场(各种通用寄存器和段寄存器ds,es,fs)


其中1、2、3由中断指令自动保存，4由中断处理函数保存。1、2、3为什么会自动保存呢？在IDT表中存储着中断号对应的中断处理函数的CS和IP，如果Intel处理器判断进入中断处理后的特权级比现在高，则会根据当前任务寄存器TR找到TSS任务段，再从TSS中找到ESP，压栈存储这些信息。(具体说明可以在INTEL编程手册中看到)

eg:
1. int 0x80的中断入口为system_call.s中的函数，用户态程序通过int 0x80进入内核的时候发生了特权级的变化，故会自动存储1、2、3
2. 时钟中断入口int 0x20为system_call.s中的_time_interrupute，也为内核代码，如果是程序在用户态发生了int 0x20中断，则会自动存储1、2、3.如果是在内核段发生了int 0x20中断，则不保存，这也是Linux的内核态的进程是不允许被调度的原因。
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191128200032428.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191128200114883.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)![在这里插入图片描述](https://img-blog.csdnimg.cn/20191128200129715.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191128200253320.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
### 1.2.3 第二阶段-调用schedule
在中断处理程序中，如果发现当前线程应该让出CPU，系统内核就会调用schedule()函数来完成TCB的切换。schedule主要干以下的事情:
1. 改变当前进程的状态
2. 寻找指向目标线程TCB的pnext
3. 调用switch_to函数切换到pnext去执行
### 1.2.4 第三阶段-内核栈的切换
即switch_to函数的功能
1. 把ESP寄存器保存在current指向的TCB中
2. 从pnext指向的TCB中取出esp字段赋给ESP寄存器
### 1.2.5 第四阶段-中断返回阶段
这个阶段是为了和中断进入阶段对应，同时为下一阶段做准备。把中断进入阶段中断处理函数压栈的寄存器弹栈
eg:
```asm
//_system_call
push %ds
push %es
push %fs
push %edx
push %ecx
push %ebx
//return_from_system_call
popl %ebx
popl %ecx
popl %edx
pop %fs
pop %es
pop %ds
```
### 1.2.6 第五阶段-用户栈切换
即用iret指令把int指令压入的ss,esp,eflags,cs,eip弹栈
## 1.3 内核级线程的创建
内核级线程的创建问题可以转为，将一个线程初始化能切换进去，且切换进去从其入口函数开始执行的样子。具体工作为:
1. 创建一个TCB，存放内核栈的esp指针
2. 分配一个内核栈，存放用户态的PC指针、用户栈地址、执行现场
3. 分配用户栈，存放进入用户态函数时用到的参数内容 

做出来这幅图即可
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191128203206520.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
## 1.4 Linux-0.11操作系统中的多进程视图
# 1.4.1 0号进程的创建
+ 0号进程PCB和内核栈的初始化
```c
struct task_struct init_stack = 
...
{{},{},{},},//LDT
{0,PAGESIZE + (long)&init.task,0x10,0,0,0...}//tss
...
```
LDT的任务是设置进程的地址空间
tss的任务是关联PCB和内核栈(PCB和内核栈放在一页内存中)
+ 0号进程用户栈的初始化

//学完内存管理之后再来看
# 1.4.2 1号进程的创建
```c
main()
{
	...
	move_to_user_mode();
	if(!fork()){
		init();
	}
	for(;;)pause;
}
```
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129081433312.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
# 1.4.3 其他进程的创建
```c
void init(void){
	execve("/bin/sh",argv_rc,envp_rc);
	....
}
```

# 2 Linux-0.11中的进程切换
执行实际进程切换的任务由 switch_to()宏定义的一段汇编代码完成。在进行切换之前，switch_to() 首先检查要切换到的进程是否就是当前进程，如果是则什么也不做，直接退出。否则就首先把内核全局 变量 current 置为新任务的指针，然后长跳转到新任务的任务状态段 TSS 组成的地址处，造成 CPU执行 任务切换操作。此时CPU会把其所有寄存器的状态保存到当前任务寄存器TR中TSS 段选择符所指向的 当前进程任务数据结构的 tss 结构中，然后把新任务状态段选择符所指向的新任务数据结构中 tss 结构中
的寄存器信息恢复到 CPU中，系统就正式开始运行新切换的任务了。这个过程可参见下图。
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129082815972.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
# 3 基于内核栈完成进程切换
# 3.1 为什么要修改原有的进程切换方式
Linux-0.11的进程切换不是基于内核栈完成的切换，而是通过intel提供的基于tss的ljmp指令来完成进程的切换。函数switch_to的核心指令ljmp的指令周期有200多个，太慢了。
# 3.2 要做的内容
1. 重写switch_to函数
2. 把switch_to函数与schedule函数链接在一起
3. 修改fork()。如果不修改fork，那么fork出的新进程无法满足新的switch_to能够切换进入的状态
# 3.3 内核栈切换的图示(精华)
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129084007539.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129084157847.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129084211158.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129084223713.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129084236888.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129084250808.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129084309881.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129084324288.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129084353720.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129084405813.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129084420774.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129084437423.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129084451214.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129084502381.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
![在这里插入图片描述](https://img-blog.csdnimg.cn/2019112908451613.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129084528299.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)![在这里插入图片描述](https://img-blog.csdnimg.cn/2019112908454310.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)![在这里插入图片描述](https://img-blog.csdnimg.cn/2019112908455940.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129084613504.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129084626629.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
![在这里插入图片描述](https://img-blog.csdnimg.cn/2019112908463438.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129084649923.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
![在这里插入图片描述](https://img-blog.csdnimg.cn/2019112908470762.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
# 3.3 修改PCB的定义和INIT_STACK宏
在Linux-0.11中，内核栈的切换由TSS完成，现在不使用TSS来切换内核栈，所以没有保存内核栈指针的信息。故要在Linux的task_struct添加一个pKernelStack字段，指向内核栈的栈顶。之所以在第四个位置添加，是因为前三个部分设计的硬编码很多，不方便修改。
该文件在/include/linux/sched.h中
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129142036128.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
至于内核栈的栈基址，我们把task_struct和内核栈放在同一页内存中。
```c
struct task_struct * p = (sttask_struct *)get_free_page();
"一页内存是4096 Bytes,PAGE_SIZE = 4096"
p->kernlstack = (long *)(PAGE_SIZE + (long)p)
"(long)p + 4096即为栈指针" 
```
因为修改了PCB，还要修改INIT_STACK宏，给0号进程的pKernelStack设定初值。
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129142227635.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)

# 3.4 重写switch_to
原来的switch_to是位于sched.h中的，现在要修改switch_to，因为内核栈涉及到精细的控制，所以要用汇编来实现switch_to，所以把switch_to放到systemcall.s中实现是最合适的。
switch_to的核心工作是:
1. 切换PCB
把current指向要切换进入的PCB，用一个寄存器保存current
2. TSS中内核栈指针的重写
虽然现在不用TSS了，但是Intel的int指令还是要用到tss，解决方法是所有的进程共用0号进程的TSS，当要切换到B进程时，就把B的内核栈栈顶放到TSS中，以供int指令使用
3. 切换内核栈
把ESP保存在当前PCB的pKernelStack字段，把目标PCB的pKernelStack字段赋给ESP。
4. 切换LDT
这个需要用_LDT(pNext)宏传递给switch_to，所以现在的switch_to需要两个参数。switch_to(pNext,_LDT(pNext))
具体的工作如下：
- 删除sched.h中的switch_to
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129142625154.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
- 在sched.h中引入汇编的switch_to，参数类型不重要，C不做强制的类型检查。![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129145749502.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
- 在system_call中修改硬编码,以后还要用到tss中的内核栈基址esp0的偏移ESP0 = 4，一并添加。
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129161028431.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
- 在system_call中引入C的全局变量current、tss、last_used_math(数学工具，与主题无关)。![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129150600856.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
- 编写_switch_to并且声明其为全局变量![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129150903785.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129151121664.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
# 3.5 修改schedule()函数
switch_to函数修改了定义，需要两个参数，故sched.c中的schedule函数也要修改一下。并且在sched.c中要添加一个全局变量tss
```c
struct tss_struct * tss = &(init_task.task.tss);
```
```c
void schedule(void)
{
	int i,next,c;
	struct task_struct ** p;
	struct task_struct * pnext;//初始化,默认下个进程就是当前进程

/* check alarm, wake up any interruptible tasks that have got a signal */

	for(p = &LAST_TASK ; p > &FIRST_TASK ; --p)
		if (*p) {
			if ((*p)->alarm && (*p)->alarm < jiffies) {
					(*p)->signal |= (1<<(SIGALRM-1));
					(*p)->alarm = 0;
				}
			if (((*p)->signal & ~(_BLOCKABLE & (*p)->blocked)) &&
			(*p)->state==TASK_INTERRUPTIBLE)
				(*p)->state=TASK_RUNNING;
		}

/* this is the scheduler proper: */

	while (1) {
		c = -1;
		next = 0;
		pnext = task[next];/*pnext默认指向,如果不添加此条语句，则无法切换进程，出现卡死现象*/
		i = NR_TASKS;
		p = &task[NR_TASKS];
		while (--i) {
			if (!*--p)
				continue;
			if ((*p)->state == TASK_RUNNING && (*p)->counter > c){
				c = (*p)->counter, next = i;
				pnext = *p;
			}
		}
		if (c) break;
		for(p = &LAST_TASK ; p > &FIRST_TASK ; --p)
			if (*p)
				(*p)->counter = ((*p)->counter >> 1) +
						(*p)->priority;
	}
	switch_to(pnext,_LDT(next));
}

```
# 3.5 修改fork
对fork修改的核心是对子进程内核栈的初始化。fork的核心实现是copy_process，定义在/kernel/fork.c中，一定要注意顺序。下面是对其的修改
```c
extern void first_return_from_kernel(void);
int copy_process(int nr,long ebp,long edi,long esi,long gs,long none,
		long ebx,long ecx,long edx,
		long fs,long es,long ds,
		long eip,long cs,long eflags,long esp,long ss)
{
	struct task_struct *p;
	int i;
	struct file *f;

	p = (struct task_struct *) get_free_page();
	long* krnstack;
	
	if (!p)
		return -EAGAIN;
	
	task[nr] = p;
	*p = *current;	/* NOTE! this doesn't copy the supervisor stack */
	p->state = TASK_UNINTERRUPTIBLE;
	p->pid = last_pid;
	p->father = current->pid;
	p->counter = p->priority;
	p->signal = 0;
	p->alarm = 0;
	p->leader = 0;		/* process leadership doesn't inherit */
	p->utime = p->stime = 0;
	p->cutime = p->cstime = 0;
	p->start_time = jiffies;
	p->tss.back_link = 0;
	p->tss.esp0 = PAGE_SIZE + (long) p;
	p->tss.ss0 = 0x10;
	p->tss.eip = eip;
	p->tss.eflags = eflags;
	p->tss.eax = 0;
	p->tss.ecx = ecx;
	p->tss.edx = edx;
	p->tss.ebx = ebx;
	p->tss.esp = esp;
	p->tss.ebp = ebp;
	p->tss.esi = esi;
	p->tss.edi = edi;
	p->tss.es = es & 0xffff;
	p->tss.cs = cs & 0xffff;
	p->tss.ss = ss & 0xffff;
	p->tss.ds = ds & 0xffff;
	p->tss.fs = fs & 0xffff;
	p->tss.gs = gs & 0xffff;
	p->tss.ldt = _LDT(nr);
	p->tss.trace_bitmap = 0x80000000;
	if (last_task_used_math == current)
		__asm__("clts ; fnsave %0"::"m" (p->tss.i387));
	if (copy_mem(nr,p)) {
		task[nr] = NULL;
		free_page((long) p);
		return -EAGAIN;
	}
	/* 内核栈的初始化语句必须放在copy的后面 */
	krnstack = (long *) (PAGE_SIZE + (long) p);
    *(--krnstack) = ss & 0xffff;
    *(--krnstack) = esp;
    *(--krnstack) = eflags;
    *(--krnstack) = cs & 0xffff;
    *(--krnstack) = eip;
	*(--krnstack) = ds & 0xffff;
	*(--krnstack) = es & 0xffff;
	*(--krnstack) = fs & 0xffff;
	*(--krnstack) = gs & 0xffff;
	*(--krnstack) = esi;
	*(--krnstack) = edi;
	*(--krnstack) = edx;
	*(--krnstack) = first_return_from_kernel;
    *(--krnstack) = ebp;
    *(--krnstack) = ecx;
    *(--krnstack) = ebx;
    *(--krnstack) = 0;
	p->pKernelStack = krnstack;
	for (i=0; i<NR_OPEN;i++)
		if ((f=p->filp[i]))
			f->f_count++;
	if (current->pwd)
		current->pwd->i_count++;
	if (current->root)
		current->root->i_count++;
	if (current->executable)
		current->executable->i_count++;
	set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));
	set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&(p->ldt));
	p->state = TASK_RUNNING;	/* do this last, just in case */
	return last_pid;
}
```
还要在systemcall中编写first_return_from_kernel并声明位全局变量。这里注意fork出的新的进程使用first_return_from_kernel，原因以后再写//。
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129160735370.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
# 3.6 编译运行
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129161202548.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129161220624.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129162535163.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
# 4 归纳总结
## 4.1 遇到的问题
### 4.1.1 汇编语言怎么调用C语言声明的全局变量?
如果在C语言中定义了全局变量A。
```c
int A;
```
在汇编中引入的方式为:
```asm
.extern _A;
```
### 4.1.2 C语言如何调用汇编语言的函数？
如果在汇编中定义了_func，想要C引用func，则需要声明其为全局的。
```asm
.globl _func
_func:
	...
	ret
```
在C中可以这样使用，注意下划线
```c
extern ret_type func(argtype arg1,....)
```
### 4.1.3. pop和popl到底是怎么工作的？即如果push 32位,pop 16位是怎么协调的？
在fork的编写中我们可以看到，内核栈初始化时压栈的**全为32位数**，而ss,cs,ds,es,fs,gs是16位的，那么在first_return_from_kernel中的pop %gs,pop% fs.....以及iret指令弹栈的时候也是pop %gs是把32位数弹出不用某16位，还是仅仅弹出16位呢？答案肯定是弹出32位，放弃高16位。为什么呢？
查阅intel编程手册，发现了答案，32位处理器的POP是32位的，如果POP到16位的寄存器，会自动过滤高16位。
![在这里插入图片描述](https://img-blog.csdnimg.cn/20191129171920189.png)
## 4.2 再谈fork
如果进程A调用fork()创建一个新的进进程B，则

- B与A共享一个用户栈。
- 在fork返回时，子进程的eax为当时pushl的0，父进程为pid。
- B可以调用exec族函数来创建一个用户栈，执行自己的程序。

![fork中父子进程对应关系](https://img-blog.csdnimg.cn/20191129173123510.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0VjdXN0X2FwcGxpZWRfbWF0aA==,size_16,color_FFFFFF,t_70)
# 5 代码地址
[代码地址](https://github.com/ecustlmc/linux-0.11-lab/tree/schedule_with_kernel_stack)
https://github.com/ecustlmc/linux-0.11-lab/tree/schedule_with_kernel_stack
