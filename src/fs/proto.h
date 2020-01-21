/* Function prototypes. */

/* Structs used in prototypes must be declared as such first. */
struct buf;
struct filp;		
struct inode;
struct super_block;

/* cache.c */
_PROTOTYPE( zone_t alloc_zone, (Dev_t dev, zone_t z)			);
_PROTOTYPE( void flushall, (Dev_t dev)					);
_PROTOTYPE( void free_zone, (Dev_t dev, zone_t numb)			);
_PROTOTYPE( struct buf *get_block, (Dev_t dev, block_t block,int only_search));
_PROTOTYPE( void invalidate, (Dev_t device)				);
_PROTOTYPE( void put_block, (struct buf *bp, int block_type)		);
_PROTOTYPE( void rw_block, (struct buf *bp, int rw_flag)		);
_PROTOTYPE( void rw_scattered, (Dev_t dev,
			struct buf **bufq, int bufqsize, int rw_flag)	);

#if ENABLE_CACHE2
/* cache2.c */
_PROTOTYPE( void init_cache2, (unsigned long size)			);
_PROTOTYPE( int get_block2, (struct buf *bp, int only_search)		);
_PROTOTYPE( void put_block2, (struct buf *bp)				);
_PROTOTYPE( void invalidate2, (Dev_t device)				);
#endif

/* device.c */
_PROTOTYPE( int dev_open, (Dev_t dev, int proc, int flags)		);
_PROTOTYPE( void dev_close, (Dev_t dev)					);
_PROTOTYPE( int dev_io, (int op, Dev_t dev, int proc, void *buf,
			off_t pos, int bytes, int flags)		);
_PROTOTYPE( int gen_opcl, (int op, Dev_t dev, int proc, int flags)	);
_PROTOTYPE( void gen_io, (int task_nr, message *mess_ptr)		);
_PROTOTYPE( int no_dev, (int op, Dev_t dev, int proc, int flags)		);
_PROTOTYPE( int tty_opcl, (int op, Dev_t dev, int proc, int flags)	);
_PROTOTYPE( int ctty_opcl, (int op, Dev_t dev, int proc, int flags)	);
_PROTOTYPE( int clone_opcl, (int op, Dev_t dev, int proc, int flags)	);
_PROTOTYPE( void ctty_io, (int task_nr, message *mess_ptr)		);
_PROTOTYPE( int do_ioctl, (void)					);
_PROTOTYPE( int do_setsid, (void)					);

/* filedes.c */
_PROTOTYPE( struct filp *find_filp, (struct inode *rip, Mode_t bits)	);
_PROTOTYPE( int get_fd, (int start, Mode_t bits, int *k, struct filp **fpt) );
_PROTOTYPE( struct filp *get_filp, (int fild)				);

/* inode.c */
_PROTOTYPE( struct inode *alloc_inode, (Dev_t dev, Mode_t bits)		);
_PROTOTYPE( void dup_inode, (struct inode *ip)				);
_PROTOTYPE( void free_inode, (Dev_t dev, Ino_t numb)			);
_PROTOTYPE( struct inode *get_inode, (Dev_t dev, int numb)		);
_PROTOTYPE( void put_inode, (struct inode *rip)				);
_PROTOTYPE( void update_times, (struct inode *rip)			);
_PROTOTYPE( void rw_inode, (struct inode *rip, int rw_flag)		);
_PROTOTYPE( void wipe_inode, (struct inode *rip)			);

/* link.c */
_PROTOTYPE( int do_link, (void)						);
_PROTOTYPE( int do_unlink, (void)					);
_PROTOTYPE( int do_rename, (void)					);
_PROTOTYPE( void truncate, (struct inode *rip)				);

/* lock.c */
_PROTOTYPE( int lock_op, (struct filp *f, int req)			);
_PROTOTYPE( void lock_revive, (void)					);

/* main.c */
_PROTOTYPE( void main, (void)						);
_PROTOTYPE( void reply, (int whom, int result)				);

/* misc.c */
_PROTOTYPE( int do_dup, (void)						);
_PROTOTYPE( int do_exit, (void)						);
_PROTOTYPE( int do_fcntl, (void)					);
_PROTOTYPE( int do_fork, (void)						);
_PROTOTYPE( int do_exec, (void)						);
_PROTOTYPE( int do_revive, (void)					);
_PROTOTYPE( int do_set, (void)						);
_PROTOTYPE( int do_sync, (void)						);
_PROTOTYPE( int do_reboot, (void)					);
_PROTOTYPE( int do_svrctl, (void)					);

/* mount.c */
_PROTOTYPE( int do_mount, (void)					);
_PROTOTYPE( int do_umount, (void)					);
_PROTOTYPE( int unmount, (Dev_t dev)					);

/* open.c */
_PROTOTYPE( int do_close, (void)					);
_PROTOTYPE( int do_creat, (void)					);
_PROTOTYPE( int do_lseek, (void)					);
_PROTOTYPE( int do_mknod, (void)					);
_PROTOTYPE( int do_mkdir, (void)					);
_PROTOTYPE( int do_open, (void)						);
#if ENABLE_SYMLINKS
_PROTOTYPE( int do_symlink, (void)                                      );
#endif /* ENABLE_SYMLINKS */

/* path.c */
_PROTOTYPE( struct inode *advance,(struct inode *dirp, char string[NAME_MAX]));
_PROTOTYPE( int search_dir, (struct inode *ldir_ptr,
			char string [NAME_MAX], ino_t *numb, int flag)	);
_PROTOTYPE( struct inode *eat_path, (char *path)			);
_PROTOTYPE( struct inode *last_dir, (char *path, char string [NAME_MAX]));
#if ENABLE_SYMLINKS
_PROTOTYPE( struct inode *slink_traverse, (struct inode *rip, char *path,
                char *string, struct inode *ldip));
#endif /* ENABLE_SYMLINKS */

/* pipe.c */
_PROTOTYPE( int do_pipe, (void)						);
_PROTOTYPE( int do_unpause, (void)					);
_PROTOTYPE( int pipe_check, (struct inode *rip, int rw_flag,
			int oflags, int bytes, off_t position, int *canwrite));
_PROTOTYPE( void release, (struct inode *ip, int call_nr, int count)	);
_PROTOTYPE( void revive, (int proc_nr, int bytes)			);
_PROTOTYPE( void suspend, (int task)					);

/* protect.c */
_PROTOTYPE( int do_access, (void)					);
_PROTOTYPE( int do_chmod, (void)					);
_PROTOTYPE( int do_chown, (void)					);
_PROTOTYPE( int do_umask, (void)					);
_PROTOTYPE( int forbidden, (struct inode *rip, Mode_t access_desired)	);
_PROTOTYPE( int read_only, (struct inode *ip)				);

/* read.c */
_PROTOTYPE( int do_read, (void)						);
_PROTOTYPE( struct buf *rahead, (struct inode *rip, block_t baseblock,
			off_t position, unsigned bytes_ahead)		);
_PROTOTYPE( void read_ahead, (void)					);
_PROTOTYPE( block_t read_map, (struct inode *rip, off_t position)	);
_PROTOTYPE( int read_write, (int rw_flag)				);
_PROTOTYPE( zone_t rd_indir, (struct buf *bp, int index)		);

/* stadir.c */
_PROTOTYPE( int do_chdir, (void)					);
_PROTOTYPE( int do_chroot, (void)					);
_PROTOTYPE( int do_fstat, (void)					);
#if ENABLE_SYMLINKS
_PROTOTYPE( int do_lstat, (void)                                        );
_PROTOTYPE( int do_rdlink, (void)                                       );
#endif /* ENABLE_SYMLINKS */
_PROTOTYPE( int do_stat, (void)						);

/* super.c */
_PROTOTYPE( bit_t alloc_bit, (struct super_block *sp, int map, bit_t origin));
_PROTOTYPE( void free_bit, (struct super_block *sp, int map,
						bit_t bit_returned)	);
_PROTOTYPE( struct super_block *get_super, (Dev_t dev)			);
_PROTOTYPE( int mounted, (struct inode *rip)				);
_PROTOTYPE( int read_super, (struct super_block *sp)			);

/* time.c */
_PROTOTYPE( int do_stime, (void)					);
_PROTOTYPE( int do_time, (void)						);
_PROTOTYPE( int do_tims, (void)						);
_PROTOTYPE( int do_utime, (void)					);

/* utility.c */
_PROTOTYPE( time_t clock_time, (void)					);
_PROTOTYPE( unsigned conv2, (int norm, int w)				);
_PROTOTYPE( long conv4, (int norm, long x)				);
_PROTOTYPE( int fetch_name, (char *path, int len, int flag)		);
_PROTOTYPE( int no_sys, (void)						);
_PROTOTYPE( void panic, (char *format, int num)				);

/* write.c */
_PROTOTYPE( void clear_zone, (struct inode *rip, off_t pos, int flag)	);
_PROTOTYPE( int do_write, (void)					);
_PROTOTYPE( struct buf *new_block, (struct inode *rip, off_t position)	);
_PROTOTYPE( void zero_block, (struct buf *bp)				);
