#include <errno.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#define MAX_SHM 100
struct shm{
	int key;
	int shmid;
	void * page;
};
struct shm shm_table[MAX_SHM] = {{0},};
int next_shm_index = 0;

int sys_create_shm(int key,unsigned long size){
	int i = 0,new_shmid = 0;
	
	if(key == 0 || next_shm_index == MAX_SHM || size > 4096)
		errno = ENOMEM;
	
	for(i = 0;i < MAX_SHM;i++){
		if(shm_table[i].key == key)
			return shm_table[i].shmid;
	}
	
	void * new_page_addr = get_free_page();
	shm_table[next_shm_index].page = new_page_addr;
	shm_table[next_shm_index].key = key;
	new_shmid = key * 3 + 47;
	shm_table[next_shm_index].shmid = new_shmid;
	next_shm_index++;
	return new_shmid;
}

void * sys_get_shm(int shmid){
	int i = 0;
	for(i = 0;i < MAX_SHM;i++){
		if(shm_table[i].shmid == shmid)
			break;
	}
	if(i == MAX_SHM)
		return NULL;
	struct shm * pShm = &shm_table[i];
	extern struct task_struct * current;
	unsigned long user_linear_addr = (unsigned long)get_base(current->ldt[1]) + current->brk;
	put_page(pShm->page,user_linear_addr);
	return current->brk;
}
