#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/threads.h>

#include "../include/virtm.h"

MODULE_DESCRIPTION("Kernsh Virtual Memory !");
MODULE_AUTHOR("pouik@kernsh.org");
MODULE_LICENSE("GPL");

static pid_t current_pid;

static struct proc_dir_entry *proc_entry_kernsh_virtm;
static struct proc_dir_entry *proc_entry_kernsh_virtm_pid;
static struct proc_dir_entry *proc_entry_kernsh_virtm_filename;
static struct proc_dir_entry *proc_entry_kernsh_virtm_dump_elf;

static char current_filename[256];

int kernsh_get_elf_image(pid_t, const char *);

static int my_atoi(const char *name)
{
    int val = 0;

    for (;; name++) {
        switch (*name) {
            case '0' ... '9':
                val = 10*val+(*name-'0');
                break;
            default:
                return val;
        }
    }
}

ssize_t kernsh_virtm_pid_write(struct file *filp, const char __user *buff, unsigned long len, void *data)
{
  int new_pid;
 
  new_pid = my_atoi(buff);

  if (new_pid > 0 && new_pid <= PID_MAX_DEFAULT)
    {
      current_pid = (pid_t) new_pid;
      printk(KERN_ALERT "PID %d\n", current_pid); 
    }
  else
    {
      return -EFAULT;
    }

  return len;
}

int kernsh_virtm_pid_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
  int len;

  len = sprintf(page, "PID = %d\n", current_pid);

  return len;
}

ssize_t kernsh_virtm_filename_write(struct file *filp, 
				    const char __user *buff, 
				    unsigned long len, 
				    void *data)
{
  if (len <= 0 || len >= sizeof(current_filename))
    {
      return -EFAULT;
    }

  if (copy_from_user(current_filename, buff, len)) 
    {
      return -EFAULT;
    }
  
  if (current_filename[len-1] == '\n')
    current_filename[len-1] = '\0';

  printk(KERN_ALERT "FILENAME %s\n", current_filename);

  return len;
}

int kernsh_virtm_filename_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
  int len;

  len = sprintf(page, "FILENAME = %s\n", current_filename);

  return len;
}

ssize_t kernsh_virtm_dump_elf_write(struct file *filp, 
				    const char __user *buff, 
				    unsigned long len, 
				    void *data)
{
  int dump;
 
  dump = my_atoi(buff);

  if (dump == 1)
    {
      if (kernsh_get_elf_image(current_pid, current_filename) < 0)
	return -EFAULT;
    }

  return len;
}

int kernsh_virtm_dump_elf_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
  int len;

  len = sprintf(page, "Please write 1 to dump elf file\n");

  return len;
}

/*
 * Dictracy Loadable Kernel Module
 *                          by -  twiz   - twiz@email.it
 *                                thefly - thefly@acaro.org
 *
 * That module let you investigate, read, search, dump and write the virtual
 * address space of a process running.
 *
 * From the idea exposed by vecna in rmfbd :
 *    http://www.s0ftpj.org/bfi/dev/BFi11-dev-06
 *
 * Thanks : silvio, Liv Tyler (by thefly)
 */

int kernsh_get_elf_image(pid_t pid, const char *filename)
{
  struct task_struct *task;
  struct file *file;
  struct mm_struct *mm;
  struct vm_area_struct *vma;
  unsigned char c;
  ssize_t ssize;
  int i;
  mm_segment_t fs;

  if (filename == NULL)
    return -1;

  printk(KERN_ALERT "DUMP pid %d @ %s\n", pid, filename);

  task = find_task_by_pid(pid);
  if (task == NULL)
    {
      printk(KERN_ALERT "Couldn't find pid %d\n", pid);
      return -1;
    }

  if (task->mm == NULL)
    return -1;
  
  file = filp_open(filename, O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO );
  if (file == NULL)
    {
      printk(KERN_ALERT "Couldn't create %s\n", filename);
      return -1;
    }

  get_file(file);

  fs = get_fs();
  set_fs(KERNEL_DS);

  mm = task->mm;

  if (mm)
    {
      for(vma = mm->mmap; vma; vma = vma->vm_next)
	{
	  /*printk(KERN_ALERT "VM_START @ 0x%lx VM_END @ 0x%lx VM_FLAGS 0x%lx VM_FILE 0x%lx\n", 
		 vma->vm_start, 
		 vma->vm_end,
		 vma->vm_flags,
		 (unsigned long)vma->vm_file);
	  */

	  if((vma->vm_flags & VM_EXECUTABLE) && 
	     (vma->vm_flags & VM_EXEC) &&
	     (vma->vm_file))
	    {
	      vma->vm_file->f_pos = 0;
	      ssize = vma->vm_file->f_op->read(vma->vm_file, &c, sizeof(c), 
					       &vma->vm_file->f_pos);
	      if(ssize > 0)
		{
		  while(ssize != 0) 
		    {
		      i = file->f_op->write(file, &c, sizeof(c), &file->f_pos );
		      if (i != ssize) 
			{
			  filp_close(file, 0);
			  return -1;
			}
		      ssize = vma->vm_file->f_op->read(vma->vm_file, &c, sizeof(c), 
						       &vma->vm_file->f_pos );
		    }
		}  
	    }
	}

    }

  set_fs(fs);
  filp_close(file, 0);

  return 0;
}

static int kernsh_virtm_init(void)
{
  printk(KERN_ALERT "Kernsh Virtm Init\n");
  
  current_pid = -1;
  memset(current_filename, '\0', sizeof(current_filename));

  proc_entry_kernsh_virtm = proc_mkdir(PROC_ENTRY_KERNSH_VIRTM, &proc_root);

  if (proc_entry_kernsh_virtm == NULL)
    {
      printk(KERN_ALERT "Couldn't create kernsh virtm proc entry\n");
      return -1;
    }
  
  proc_entry_kernsh_virtm_pid = 
    create_proc_entry(PROC_ENTRY_KERNSH_VIRTM_PID, 0644, proc_entry_kernsh_virtm);
  proc_entry_kernsh_virtm_filename = 
    create_proc_entry(PROC_ENTRY_KERNSH_VIRTM_FILENAME, 0644, proc_entry_kernsh_virtm);
  proc_entry_kernsh_virtm_dump_elf = 
    create_proc_entry(PROC_ENTRY_KERNSH_VIRTM_DUMP_ELF, 0644, proc_entry_kernsh_virtm);

  if (proc_entry_kernsh_virtm_pid == NULL || 
      proc_entry_kernsh_virtm_filename == NULL ||
      proc_entry_kernsh_virtm_dump_elf == NULL)
    {
      printk(KERN_ALERT "Couldn't create sub proc entry\n");
      return -1;
    }
  
  
  proc_entry_kernsh_virtm_pid->read_proc = kernsh_virtm_pid_read;
  proc_entry_kernsh_virtm_pid->write_proc = kernsh_virtm_pid_write;
  proc_entry_kernsh_virtm_pid->owner = THIS_MODULE;  

  proc_entry_kernsh_virtm_filename->read_proc = kernsh_virtm_filename_read;
  proc_entry_kernsh_virtm_filename->write_proc = kernsh_virtm_filename_write;
  proc_entry_kernsh_virtm_filename->owner = THIS_MODULE;  

  proc_entry_kernsh_virtm_dump_elf->read_proc = kernsh_virtm_dump_elf_read;
  proc_entry_kernsh_virtm_dump_elf->write_proc = kernsh_virtm_dump_elf_write;
  proc_entry_kernsh_virtm_dump_elf->owner = THIS_MODULE; 

  return 0;
}

static void kernsh_virtm_exit(void)
{
  printk(KERN_ALERT "Kernsh Virtm Exit\n");

  remove_proc_entry(PROC_ENTRY_KERNSH_VIRTM_PID, proc_entry_kernsh_virtm);
  remove_proc_entry(PROC_ENTRY_KERNSH_VIRTM_FILENAME, proc_entry_kernsh_virtm);
  remove_proc_entry(PROC_ENTRY_KERNSH_VIRTM_DUMP_ELF, proc_entry_kernsh_virtm);

   remove_proc_entry(PROC_ENTRY_KERNSH_VIRTM, &proc_root);
}


module_init(kernsh_virtm_init);
module_exit(kernsh_virtm_exit);