/*
  Obtained by combining, with trivial modification,
  code from github.com/stanislaw/posix-macos-addons
*/

#include "hw/avatar/macos_mqueue/mqueue.h"
#include "hw/avatar/macos_mqueue/mqueue-internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <assert.h>
#include <string.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <stdint.h>
#include <time.h>
#include <sys/file.h>

int mq_close(mqd_t mqd) {
  long msgsize, filesize;
  struct mq_hdr *mqhdr;
  struct mq_attr *attr;
  struct mq_info *mqinfo;

  mqinfo = mqd;
  if (mqinfo->mqi_magic != MQI_MAGIC) {
    errno = EBADF;
    return (-1);
  }
  mqhdr = mqinfo->mqi_hdr;
  attr = &mqhdr->mqh_attr;

  if (mq_notify(mqd, NULL) != 0) /* unregister calling process */
    return (-1);

  msgsize = MSGSIZE(attr->mq_msgsize);
  filesize = sizeof(struct mq_hdr) +
    (attr->mq_maxmsg * (sizeof(struct mymsg_hdr) + msgsize));
  if (munmap(mqinfo->mqi_hdr, filesize) == -1)
    return (-1);

  mqinfo->mqi_magic = 0; /* just in case */
  free(mqinfo);
  return (0);
}

int
mq_getattr(mqd_t mqd, struct mq_attr *mqstat)
{
	int		n;
	struct mq_hdr	*mqhdr;
	struct mq_attr	*attr;
	struct mq_info	*mqinfo;

	mqinfo = mqd;
	if (mqinfo->mqi_magic != MQI_MAGIC) {
		errno = EBADF;
		return(-1);
	}
	mqhdr = mqinfo->mqi_hdr;
	attr = &mqhdr->mqh_attr;
	if ( (n = pthread_mutex_lock(&mqhdr->mqh_lock)) != 0) {
		errno = n;
		return(-1);
	}

	mqstat->mq_flags = mqinfo->mqi_flags;	/* per-open */
	mqstat->mq_maxmsg = attr->mq_maxmsg;	/* remaining three per-queue */
	mqstat->mq_msgsize = attr->mq_msgsize;
	mqstat->mq_curmsgs = attr->mq_curmsgs;

	pthread_mutex_unlock(&mqhdr->mqh_lock);
	return(0);
}

const size_t MQ_FS_NAME_MAX = 64;

static const char prefix[] = "/tmp";

static const size_t MQ_NAME_MAX = 32;

static size_t safe_strlen(const char *str, size_t max_len) {
  const char *end = (const char *)memchr(str, '\0', max_len);
  if (end == NULL) {
    return max_len;
  } else {
    return end - str;
  }
}

int mq_get_fs_pathname(const char *const pathname, char *const out_pathname) {
  if (pathname[0] != '/') {
    return EINVAL;
  }

  size_t pathname_len = safe_strlen(pathname, MQ_NAME_MAX);
  assert(pathname_len < MQ_NAME_MAX);

  size_t internal_len = strlen(prefix) + pathname_len + 1; // +1 for ending '\0'
  assert(internal_len < MQ_FS_NAME_MAX);

  snprintf(out_pathname, internal_len, "%s%s", prefix, pathname);
  return 0;
}

int mq_notify(mqd_t mqd, const struct sigevent *notification) {
  int n;
  pid_t pid;
  struct mq_hdr *mqhdr;
  struct mq_info *mqinfo;

  mqinfo = mqd;
  if (mqinfo->mqi_magic != MQI_MAGIC) {
    errno = EBADF;
    return (-1);
  }
  mqhdr = mqinfo->mqi_hdr;
  if ((n = pthread_mutex_lock(&mqhdr->mqh_lock)) != 0) {
    errno = n;
    return (-1);
  }

  pid = getpid();
  if (notification == NULL) {
    if (mqhdr->mqh_pid == pid) {
      mqhdr->mqh_pid = 0; /* unregister calling process */
    } /* no error if caller not registered */
  } else {
    if (mqhdr->mqh_pid != 0) {
      if (kill(mqhdr->mqh_pid, 0) != -1 || errno != ESRCH) {
        errno = EBUSY;
        goto mq_notify_err;
      }
    }
    mqhdr->mqh_pid = pid;
    mqhdr->mqh_event = *notification;
  }
  pthread_mutex_unlock(&mqhdr->mqh_lock);
  return (0);

mq_notify_err:
  pthread_mutex_unlock(&mqhdr->mqh_lock);
  return (-1);
}

#define MAX_TRIES 10 /* for waiting for initialization */

/// TODO: CHECK THIS
#define va_mode_t int

struct mq_attr defattr = {0, 128, 1024, 0};

mqd_t mq_open(const char *pathname, int oflag, ...) {
  int i, fd, nonblock, created, save_errno;
  long msgsize, filesize, index;
  va_list ap;
  mode_t mode;
  int8_t *mptr;
  struct stat statbuff;
  struct mq_hdr *mqhdr;
  struct mymsg_hdr *msghdr;
  struct mq_attr *attr;
  struct mq_info *mqinfo;
  pthread_mutexattr_t mattr;
  pthread_condattr_t cattr;

  created = 0;
  nonblock = oflag & O_NONBLOCK;
  oflag &= ~O_NONBLOCK;
  mptr = (int8_t *)MAP_FAILED;
  mqinfo = NULL;

  char fs_pathname[MQ_FS_NAME_MAX];
  if (mq_get_fs_pathname(pathname, fs_pathname) == EINVAL) {
    errno = EINVAL;
    return ((mqd_t)-1);
  };

mq_open_again:
  if (oflag & O_CREAT) {
    va_start(ap, oflag); /* init ap to final named argument */
    mode = va_arg(ap, va_mode_t) & ~S_IXUSR;
    attr = va_arg(ap, struct mq_attr *);
    va_end(ap);

    /* open and specify O_EXCL and user-execute */
    fd = open(fs_pathname, oflag | O_EXCL | O_RDWR, mode | S_IXUSR);
    if (fd < 0) {
      if (errno == EEXIST && (oflag & O_EXCL) == 0)
        goto mq_open_exists; /* already exists, OK */
      else
        return ((mqd_t)-1);
    }
    created = 1;
    /* first one to create the file initializes it */
    if (attr == NULL)
      attr = &defattr;
    else {
      if (attr->mq_maxmsg <= 0 || attr->mq_msgsize <= 0) {
        errno = EINVAL;
        goto mq_open_err;
      }
    }

    /* calculate and set the file size */
    msgsize = MSGSIZE(attr->mq_msgsize);
    filesize = sizeof(struct mq_hdr) +
      (attr->mq_maxmsg * (sizeof(struct mymsg_hdr) + msgsize));
    if (lseek(fd, filesize - 1, SEEK_SET) == -1)
      goto mq_open_err;
    if (write(fd, "", 1) == -1)
      goto mq_open_err;

    /* memory map the file */
    mptr = mmap(NULL, filesize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mptr == MAP_FAILED)
      goto mq_open_err;

    /* allocate one mq_info{} for the queue */
    /* *INDENT-OFF* */
    if ((mqinfo = malloc(sizeof(struct mq_info))) == NULL)
      goto mq_open_err;
    /* *INDENT-ON* */
    mqinfo->mqi_hdr = mqhdr = (struct mq_hdr *)mptr;
    mqinfo->mqi_magic = MQI_MAGIC;
    mqinfo->mqi_flags = nonblock;

    /* initialize header at beginning of file */
    /* create free list with all messages on it */
    mqhdr->mqh_attr.mq_flags = 0;
    mqhdr->mqh_attr.mq_maxmsg = attr->mq_maxmsg;
    mqhdr->mqh_attr.mq_msgsize = attr->mq_msgsize;
    mqhdr->mqh_attr.mq_curmsgs = 0;
    mqhdr->mqh_nwait = 0;
    mqhdr->mqh_pid = 0;
    mqhdr->mqh_head = 0;
    index = sizeof(struct mq_hdr);
    mqhdr->mqh_free = index;
    for (i = 0; i < attr->mq_maxmsg - 1; i++) {
      msghdr = (struct mymsg_hdr *)&mptr[index];
      index += sizeof(struct mymsg_hdr) + msgsize;
      msghdr->msg_next = index;
    }
    msghdr = (struct mymsg_hdr *)&mptr[index];
    msghdr->msg_next = 0; /* end of free list */

    /* initialize mutex & condition variable */
    if ((i = pthread_mutexattr_init(&mattr)) != 0)
      goto mq_open_pthreaderr;
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    i = pthread_mutex_init(&mqhdr->mqh_lock, &mattr);
    pthread_mutexattr_destroy(&mattr); /* be sure to destroy */
    if (i != 0)
      goto mq_open_pthreaderr;

    if ((i = pthread_condattr_init(&cattr)) != 0)
      goto mq_open_pthreaderr;
    pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
    i = pthread_cond_init(&mqhdr->mqh_wait, &cattr);
    pthread_condattr_destroy(&cattr); /* be sure to destroy */
    if (i != 0)
      goto mq_open_pthreaderr;

    /* initialization complete, turn off user-execute bit */
    if (fchmod(fd, mode) == -1)
      goto mq_open_err;
    close(fd);
    return ((mqd_t)mqinfo);
  }

mq_open_exists:
  /* open the file then memory map */
  if ((fd = open(fs_pathname, O_RDWR)) < 0) {
    if (errno == ENOENT && (oflag & O_CREAT))
      goto mq_open_again;
    goto mq_open_err;
  }

  /* make certain initialization is complete */
  for (i = 0; i < MAX_TRIES; i++) {
    if (stat(fs_pathname, &statbuff) == -1) {
      if (errno == ENOENT && (oflag & O_CREAT)) {
        close(fd);
        goto mq_open_again;
      }
      goto mq_open_err;
    }
    if ((statbuff.st_mode & S_IXUSR) == 0)
      break;
    sleep(1);
  }
  if (i == MAX_TRIES) {
    errno = ETIMEDOUT;
    goto mq_open_err;
  }

  filesize = statbuff.st_size;
  mptr = mmap(NULL, filesize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (mptr == MAP_FAILED)
    goto mq_open_err;
  close(fd);

  /* allocate one mq_info{} for each open */
  if ((mqinfo = malloc(sizeof(struct mq_info))) == NULL)
    goto mq_open_err;
  mqinfo->mqi_hdr = (struct mq_hdr *)mptr;
  mqinfo->mqi_magic = MQI_MAGIC;
  mqinfo->mqi_flags = nonblock;
  return ((mqd_t)mqinfo);

mq_open_pthreaderr:
  errno = i;

mq_open_err:
  /* don't let following function calls change errno */
  save_errno = errno;
  if (created)
    unlink(fs_pathname);
  if (mptr != MAP_FAILED)
    munmap(mptr, filesize);
  if (mqinfo != NULL)
    free(mqinfo);
  close(fd);
  errno = save_errno;
  return ((mqd_t)-1);
}

ssize_t mq_receive(mqd_t mqd, char *ptr, size_t maxlen, unsigned int *priop) {
  int n;
  long index;
  int8_t *mptr;
  ssize_t len;
  struct mq_hdr *mqhdr;
  struct mq_attr *attr;
  struct mymsg_hdr *msghdr;
  struct mq_info *mqinfo;

  mqinfo = mqd;
  if (mqinfo->mqi_magic != MQI_MAGIC) {
    errno = EBADF;
    return (-1);
  }
  mqhdr = mqinfo->mqi_hdr; /* struct pointer */
  mptr = (int8_t *)mqhdr; /* byte pointer */
  attr = &mqhdr->mqh_attr;
  if ((n = pthread_mutex_lock(&mqhdr->mqh_lock)) != 0) {
    errno = n;
    return (-1);
  }

  if (maxlen < attr->mq_msgsize) {
    errno = EMSGSIZE;
    goto mq_receive_err;
  }
  if (attr->mq_curmsgs == 0) { /* queue is empty */
    if (mqinfo->mqi_flags & O_NONBLOCK) {
      errno = EAGAIN;
      goto mq_receive_err;
    }
    /* wait for a message to be placed onto queue */
    mqhdr->mqh_nwait++;
    while (attr->mq_curmsgs == 0)
      pthread_cond_wait(&mqhdr->mqh_wait, &mqhdr->mqh_lock);
    mqhdr->mqh_nwait--;
  }

  if ((index = mqhdr->mqh_head) == 0) {
    printf("mq_receive: curmsgs = %ld; head = 0\n", attr->mq_curmsgs);
  }

  msghdr = (struct mymsg_hdr *)&mptr[index];
  mqhdr->mqh_head = msghdr->msg_next; /* new head of list */
  len = msghdr->msg_len;
  memcpy(ptr, msghdr + 1, len); /* copy the message itself */
  if (priop != NULL)
    *priop = msghdr->msg_prio;

  /* just-read message goes to front of free list */
  msghdr->msg_next = mqhdr->mqh_free;
  mqhdr->mqh_free = index;

  /* wake up anyone blocked in mq_send waiting for room */
  if (attr->mq_curmsgs == attr->mq_maxmsg)
    pthread_cond_signal(&mqhdr->mqh_wait);
  attr->mq_curmsgs--;

  pthread_mutex_unlock(&mqhdr->mqh_lock);
  return (len);

mq_receive_err:
  pthread_mutex_unlock(&mqhdr->mqh_lock);
  return (-1);
}

int mq_send(mqd_t mqd, const char *ptr, size_t len, unsigned int prio) {
  assert(sizeof(((struct timespec *)0)->tv_sec) == 8);

  struct timespec distant_future;
  distant_future.tv_nsec = 0;
  distant_future.tv_sec = (int64_t)(UINT64_MAX / 2);

  return mq_timedsend(mqd, ptr, len, prio, &distant_future);
}

int
mq_setattr(mqd_t mqd, const struct mq_attr *mqstat,
			 struct mq_attr *omqstat)
{
	int		n;
	struct mq_hdr	*mqhdr;
	struct mq_attr	*attr;
	struct mq_info	*mqinfo;

	mqinfo = mqd;
	if (mqinfo->mqi_magic != MQI_MAGIC) {
		errno = EBADF;
		return(-1);
	}
	mqhdr = mqinfo->mqi_hdr;
	attr = &mqhdr->mqh_attr;
	if ( (n = pthread_mutex_lock(&mqhdr->mqh_lock)) != 0) {
		errno = n;
		return(-1);
	}

	if (omqstat != NULL) {
		omqstat->mq_flags = mqinfo->mqi_flags;	/* previous attributes */
		omqstat->mq_maxmsg = attr->mq_maxmsg;
		omqstat->mq_msgsize = attr->mq_msgsize;
		omqstat->mq_curmsgs = attr->mq_curmsgs;	/* and current status */
	}

	if (mqstat->mq_flags & O_NONBLOCK)
		mqinfo->mqi_flags |= O_NONBLOCK;
	else
		mqinfo->mqi_flags &= ~O_NONBLOCK;

	pthread_mutex_unlock(&mqhdr->mqh_lock);
	return(0);
}

ssize_t mq_timedreceive(mqd_t mqd,
                        char *ptr,
                        size_t maxlen,
                        unsigned *priop,
                        const struct timespec *abs_timeout) {
  int n;
  long index;
  int8_t *mptr;
  ssize_t len;
  struct mq_hdr *mqhdr;
  struct mq_attr *attr;
  struct mymsg_hdr *msghdr;
  struct mq_info *mqinfo;

  mqinfo = mqd;
  if (mqinfo->mqi_magic != MQI_MAGIC) {
    errno = EBADF;
    return (-1);
  }
  mqhdr = mqinfo->mqi_hdr; /* struct pointer */
  mptr = (int8_t *)mqhdr; /* byte pointer */
  attr = &mqhdr->mqh_attr;
  if ((n = pthread_mutex_lock(&mqhdr->mqh_lock)) != 0) {
    errno = n;
    return (-1);
  }

  if (maxlen < attr->mq_msgsize) {
    errno = EMSGSIZE;
    goto mq_timedreceive_err;
  }
  if (attr->mq_curmsgs == 0) { /* queue is empty */
    if (mqinfo->mqi_flags & O_NONBLOCK) {
      errno = EAGAIN;
      goto mq_timedreceive_err;
    }
    /* wait for a message to be placed onto queue */
    mqhdr->mqh_nwait++;
    while (attr->mq_curmsgs == 0) {
      int wait_result =
        pthread_cond_timedwait(&mqhdr->mqh_wait, &mqhdr->mqh_lock, abs_timeout);
      if (wait_result == ETIMEDOUT) {
        errno = ETIMEDOUT;
        goto mq_timedreceive_err;
      }
      assert(wait_result == 0);
    }
    mqhdr->mqh_nwait--;
  }

  if ((index = mqhdr->mqh_head) == 0) {
    printf("mq_receive: curmsgs = %ld; head = 0\n", attr->mq_curmsgs);
  }

  msghdr = (struct mymsg_hdr *)&mptr[index];
  mqhdr->mqh_head = msghdr->msg_next; /* new head of list */
  len = msghdr->msg_len;
  memcpy(ptr, msghdr + 1, len); /* copy the message itself */
  if (priop != NULL)
    *priop = msghdr->msg_prio;

  /* just-read message goes to front of free list */
  msghdr->msg_next = mqhdr->mqh_free;
  mqhdr->mqh_free = index;

  /* wake up anyone blocked in mq_send waiting for room */
  if (attr->mq_curmsgs == attr->mq_maxmsg) {
    assert(pthread_cond_signal(&mqhdr->mqh_wait) == 0);
  }
  attr->mq_curmsgs--;

  assert(pthread_mutex_unlock(&mqhdr->mqh_lock) == 0);
  return (len);

mq_timedreceive_err:
  pthread_mutex_unlock(&mqhdr->mqh_lock);
  return (-1);
}

int mq_timedsend(mqd_t mqd,
                 const char *ptr,
                 size_t len,
                 unsigned prio,
                 const struct timespec *abs_timeout) {
  int n;
  long index, freeindex;
  int8_t *mptr;
  struct sigevent *sigev;
  struct mq_hdr *mqhdr;
  struct mq_attr *attr;
  struct mymsg_hdr *msghdr, *nmsghdr, *pmsghdr;
  struct mq_info *mqinfo;

  mqinfo = mqd;
  if (mqinfo->mqi_magic != MQI_MAGIC) {
    errno = EBADF;
    return (-1);
  }
  mqhdr = mqinfo->mqi_hdr; /* struct pointer */
  mptr = (int8_t *)mqhdr; /* byte pointer */
  attr = &mqhdr->mqh_attr;
  if ((n = pthread_mutex_lock(&mqhdr->mqh_lock)) != 0) {
    errno = n;
    return (-1);
  }

  if (len > attr->mq_msgsize) {
    errno = EMSGSIZE;
    goto mq_timedsend_err;
  }
  if (attr->mq_curmsgs == 0) {
    if (mqhdr->mqh_pid != 0 && mqhdr->mqh_nwait == 0) {
      sigev = &mqhdr->mqh_event;
      if (sigev->sigev_notify == SIGEV_SIGNAL) {
        /// sigqueue does not exit on macOS but it looks like it is enough if we
        /// just send a signal with kill to make simple tests pass.
        /// If a user does not use mq_notify this can be considered an unused
        /// branch.
        /// sigqueue(mqhdr->mqh_pid, sigev->sigev_signo, sigev->sigev_value);
        kill(mqhdr->mqh_pid, sigev->sigev_signo);
      }
      mqhdr->mqh_pid = 0; /* unregister */
    }
  } else if (attr->mq_curmsgs >= attr->mq_maxmsg) {
    /* queue is full */
    if (mqinfo->mqi_flags & O_NONBLOCK) {
      errno = EAGAIN;
      goto mq_timedsend_err;
    }
    /* wait for room for one message on the queue */
    while (attr->mq_curmsgs >= attr->mq_maxmsg) {
      int wait_result =
        pthread_cond_timedwait(&mqhdr->mqh_wait, &mqhdr->mqh_lock, abs_timeout);

      if (wait_result == ETIMEDOUT) {
        errno = ETIMEDOUT;
        goto mq_timedsend_err;
      }
    }
  }

  /* nmsghdr will point to new message */
  if ((freeindex = mqhdr->mqh_free) == 0) {
    printf("mq_send: curmsgs = %ld; free = 0\n", attr->mq_curmsgs);
  }
  nmsghdr = (struct mymsg_hdr *)&mptr[freeindex];
  nmsghdr->msg_prio = prio;
  nmsghdr->msg_len = len;
  memcpy(nmsghdr + 1, ptr, len); /* copy message from caller */
  mqhdr->mqh_free = nmsghdr->msg_next; /* new freelist head */

  /* find right place for message in linked list */
  index = mqhdr->mqh_head;
  pmsghdr = (struct mymsg_hdr *)&(mqhdr->mqh_head);
  while (index != 0) {
    msghdr = (struct mymsg_hdr *)&mptr[index];
    if (prio > msghdr->msg_prio) {
      nmsghdr->msg_next = index;
      pmsghdr->msg_next = freeindex;
      break;
    }
    index = msghdr->msg_next;
    pmsghdr = msghdr;
  }
  if (index == 0) {
    /* queue was empty or new goes at end of list */
    pmsghdr->msg_next = freeindex;
    nmsghdr->msg_next = 0;
  }
  /* wake up anyone blocked in mq_receive waiting for a message */
  if (attr->mq_curmsgs == 0) {
    pthread_cond_signal(&mqhdr->mqh_wait);
  }
  attr->mq_curmsgs++;

  pthread_mutex_unlock(&mqhdr->mqh_lock);
  return (0);

mq_timedsend_err:
  pthread_mutex_unlock(&mqhdr->mqh_lock);
  return (-1);
}

int mq_unlink(const char *pathname) {
  char fs_pathname[MQ_FS_NAME_MAX];
  if (mq_get_fs_pathname(pathname, fs_pathname) == EINVAL) {
    errno = EINVAL;
    return -1;
  };
  if (unlink(fs_pathname) == -1) {
    return -1;
  }
  return 0;
}
