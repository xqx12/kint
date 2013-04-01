/* commit: a1f74ae82d133ebb2aabb19d181944b4e83e9960 */
/* date: Tue, 5 Apr 2011 16:45:59 +0000 */
# 4 "drivers/scsi/mpt2sas/mpt2sas_ctl.c"

static DEFINE_MUTEX(_ctl_mutex);
static struct fasync_struct *async_queue;
static DECLARE_WAIT_QUEUE_HEAD(ctl_poll_wait);

static int _ctl_send_release(struct MPT2SAS_ADAPTER *ioc, u8 buffer_type,
    u8 *issue_reset);

/**
 * enum block_state - blocking state
 * @NON_BLOCKING: non blocking
 * @BLOCKING: blocking
 *
 * These states are for ioctls that need to wait for a response
 * from firmware, so they probably require sleep.
 */
enum block_state {
	NON_BLOCKING,
	BLOCKING,
};

//# 541
/**
 * _ctl_set_task_mid - assign an active smid to tm request
 * @ioc: per adapter object
 * @karg - (struct mpt2_ioctl_command)
 * @tm_request - pointer to mf from user space
 *
 * Returns 0 when an smid if found, else fail.
 * during failure, the reply frame is filled.
 */
static int
_ctl_set_task_mid(struct MPT2SAS_ADAPTER *ioc, struct mpt2_ioctl_command *karg,
    Mpi2SCSITaskManagementRequest_t *tm_request)
{
	u8 found = 0;
	u16 i;
	u16 handle;
	struct scsi_cmnd *scmd;
	struct MPT2SAS_DEVICE *priv_data;
	unsigned long flags;
	Mpi2SCSITaskManagementReply_t *tm_reply;
	u32 sz;
	u32 lun;
	char *desc = NULL;

	if (tm_request->TaskType == MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK)
		desc = "abort_task";
	else if (tm_request->TaskType == MPI2_SCSITASKMGMT_TASKTYPE_QUERY_TASK)
		desc = "query_task";
	else
		return 0;

	lun = scsilun_to_int((struct scsi_lun *)tm_request->LUN);

	handle = le16_to_cpu(tm_request->DevHandle);
	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	for (i = ioc->scsiio_depth; i && !found; i--) {
		scmd = ioc->scsi_lookup[i - 1].scmd;
		if (scmd == NULL || scmd->device == NULL ||
		    scmd->device->hostdata == NULL)
			continue;
		if (lun != scmd->device->lun)
			continue;
		priv_data = scmd->device->hostdata;
		if (priv_data->sas_target == NULL)
			continue;
		if (priv_data->sas_target->handle != handle)
			continue;
		tm_request->TaskMID = cpu_to_le16(ioc->scsi_lookup[i - 1].smid);
		found = 1;
	}

	dctlprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: "
	    "handle(0x%04x), lun(%d), task_mid(%d)\n", ioc->name,
	    desc, le16_to_cpu(tm_request->DevHandle), lun,
	     le16_to_cpu(tm_request->TaskMID)));
	return 0;
}

//# 619
/**
 * _ctl_do_mpt_command - main handler for MPT2COMMAND opcode
 * @ioc: per adapter object
 * @karg - (struct mpt2_ioctl_command)
 * @mf - pointer to mf in user space
 * @state - NON_BLOCKING or BLOCKING
 */
/*static*/ long
_ctl_do_mpt_command(struct MPT2SAS_ADAPTER *ioc,
    struct mpt2_ioctl_command karg, void __user *mf, enum block_state state)
{
	MPI2RequestHeader_t *mpi_request = NULL, *request;
	MPI2DefaultReply_t *mpi_reply;
	u32 ioc_state;
	u16 ioc_status;
	u16 smid;
	unsigned long timeout, timeleft;
	u8 issue_reset;
	u32 sz;
	void *psge;
	void *data_out = NULL;
	dma_addr_t data_out_dma;
	size_t data_out_sz = 0;
	void *data_in = NULL;
	dma_addr_t data_in_dma;
	size_t data_in_sz = 0;
	u32 sgl_flags;
	long ret;
	u16 wait_state_count;

	issue_reset = 0;

	if (state == NON_BLOCKING && !mutex_trylock(&ioc->ctl_cmds.mutex))
		return -EAGAIN;
	else if (mutex_lock_interruptible(&ioc->ctl_cmds.mutex))
		return -ERESTARTSYS;

	if (ioc->ctl_cmds.status != MPT2_CMD_NOT_USED) {
		printk(MPT2SAS_ERR_FMT "%s: ctl_cmd in use\n",
		    ioc->name, __func__);
		ret = -EAGAIN;
		goto out;
	}

	wait_state_count = 0;
	ioc_state = mpt2sas_base_get_iocstate(ioc, 1);
	while (ioc_state != MPI2_IOC_STATE_OPERATIONAL) {
		if (wait_state_count++ == 10) {
			printk(MPT2SAS_ERR_FMT
			    "%s: failed due to ioc not operational\n",
			    ioc->name, __func__);
			ret = -EFAULT;
			goto out;
		}
		ssleep(1);
		ioc_state = mpt2sas_base_get_iocstate(ioc, 1);
		printk(MPT2SAS_INFO_FMT "%s: waiting for "
		    "operational state(count=%d)\n", ioc->name,
		    __func__, wait_state_count);
	}
	if (wait_state_count)
		printk(MPT2SAS_INFO_FMT "%s: ioc is operational\n",
		    ioc->name, __func__);

	mpi_request = kzalloc(ioc->request_sz, GFP_KERNEL);
	if (!mpi_request) {
		printk(MPT2SAS_ERR_FMT "%s: failed obtaining a memory for "
		    "mpi_request\n", ioc->name, __func__);
		ret = -ENOMEM;
		goto out;
	}
#ifdef __PATCH__
	/* Check for overflow and wraparound */
	if (karg.data_sge_offset * 4 > ioc->request_sz ||
	    karg.data_sge_offset > (UINT_MAX / 4)) {
		ret = -EINVAL;
		goto out;
	}
#endif
//# 698
	/* copy in request message frame from user */
	if (copy_from_user(mpi_request, mf, karg.data_sge_offset*4)) {
		printk(KERN_ERR "failure at %s:%d/%s()!\n", __FILE__, __LINE__,
		    __func__);
		ret = -EFAULT;
		goto out;
	}
#if 0
	if (mpi_request->Function == MPI2_FUNCTION_SCSI_TASK_MGMT) {
		smid = mpt2sas_base_get_smid_hpr(ioc, ioc->ctl_cb_idx);
		if (!smid) {
			printk(MPT2SAS_ERR_FMT "%s: failed obtaining a smid\n",
			    ioc->name, __func__);
			ret = -EAGAIN;
			goto out;
		}
	} else {

		smid = mpt2sas_base_get_smid_scsiio(ioc, ioc->ctl_cb_idx, NULL);
		if (!smid) {
			printk(MPT2SAS_ERR_FMT "%s: failed obtaining a smid\n",
			    ioc->name, __func__);
			ret = -EAGAIN;
			goto out;
		}
	}

	ret = 0;
	ioc->ctl_cmds.status = MPT2_CMD_PENDING;
	memset(ioc->ctl_cmds.reply, 0, ioc->reply_sz);
	request = mpt2sas_base_get_msg_frame(ioc, smid);
	memcpy(request, mpi_request, karg.data_sge_offset*4);
	ioc->ctl_cmds.smid = smid;
	data_out_sz = karg.data_out_size;
	data_in_sz = karg.data_in_size;

	if (mpi_request->Function == MPI2_FUNCTION_SCSI_IO_REQUEST ||
	    mpi_request->Function == MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH) {
		if (!le16_to_cpu(mpi_request->FunctionDependent1) ||
		    le16_to_cpu(mpi_request->FunctionDependent1) >
		    ioc->facts.MaxDevHandle) {
			ret = -EINVAL;
			mpt2sas_base_free_smid(ioc, smid);
			goto out;
		}
	}

	/* obtain dma-able memory for data transfer */
	if (data_out_sz) /* WRITE */ {
		data_out = pci_alloc_consistent(ioc->pdev, data_out_sz,
		    &data_out_dma);
		if (!data_out) {
			printk(KERN_ERR "failure at %s:%d/%s()!\n", __FILE__,
			    __LINE__, __func__);
			ret = -ENOMEM;
			mpt2sas_base_free_smid(ioc, smid);
			goto out;
		}
		if (copy_from_user(data_out, karg.data_out_buf_ptr,
			data_out_sz)) {
			printk(KERN_ERR "failure at %s:%d/%s()!\n", __FILE__,
			    __LINE__, __func__);
			ret =  -EFAULT;
			mpt2sas_base_free_smid(ioc, smid);
			goto out;
		}
	}

	if (data_in_sz) /* READ */ {
		data_in = pci_alloc_consistent(ioc->pdev, data_in_sz,
		    &data_in_dma);
		if (!data_in) {
			printk(KERN_ERR "failure at %s:%d/%s()!\n", __FILE__,
			    __LINE__, __func__);
			ret = -ENOMEM;
			mpt2sas_base_free_smid(ioc, smid);
			goto out;
		}
	}

	/* add scatter gather elements */
	psge = (void *)request + (karg.data_sge_offset*4);

	if (!data_out_sz && !data_in_sz) {
		mpt2sas_base_build_zero_len_sge(ioc, psge);
	} else if (data_out_sz && data_in_sz) {
		/* WRITE sgel first */
		sgl_flags = (MPI2_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI2_SGE_FLAGS_END_OF_BUFFER | MPI2_SGE_FLAGS_HOST_TO_IOC);
		sgl_flags = sgl_flags << MPI2_SGE_FLAGS_SHIFT;
		ioc->base_add_sg_single(psge, sgl_flags |
		    data_out_sz, data_out_dma);

		/* incr sgel */
		psge += ioc->sge_size;

		/* READ sgel last */
		sgl_flags = (MPI2_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI2_SGE_FLAGS_LAST_ELEMENT | MPI2_SGE_FLAGS_END_OF_BUFFER |
		    MPI2_SGE_FLAGS_END_OF_LIST);
		sgl_flags = sgl_flags << MPI2_SGE_FLAGS_SHIFT;
		ioc->base_add_sg_single(psge, sgl_flags |
		    data_in_sz, data_in_dma);
	} else if (data_out_sz) /* WRITE */ {
		sgl_flags = (MPI2_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI2_SGE_FLAGS_LAST_ELEMENT | MPI2_SGE_FLAGS_END_OF_BUFFER |
		    MPI2_SGE_FLAGS_END_OF_LIST | MPI2_SGE_FLAGS_HOST_TO_IOC);
		sgl_flags = sgl_flags << MPI2_SGE_FLAGS_SHIFT;
		ioc->base_add_sg_single(psge, sgl_flags |
		    data_out_sz, data_out_dma);
	} else if (data_in_sz) /* READ */ {
		sgl_flags = (MPI2_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI2_SGE_FLAGS_LAST_ELEMENT | MPI2_SGE_FLAGS_END_OF_BUFFER |
		    MPI2_SGE_FLAGS_END_OF_LIST);
		sgl_flags = sgl_flags << MPI2_SGE_FLAGS_SHIFT;
		ioc->base_add_sg_single(psge, sgl_flags |
		    data_in_sz, data_in_dma);
	}

	/* send command to firmware */
#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
	_ctl_display_some_debug(ioc, smid, "ctl_request", NULL);
#endif

	switch (mpi_request->Function) {
	case MPI2_FUNCTION_SCSI_IO_REQUEST:
	case MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH:
	{
		Mpi2SCSIIORequest_t *scsiio_request =
		    (Mpi2SCSIIORequest_t *)request;
		scsiio_request->SenseBufferLength = SCSI_SENSE_BUFFERSIZE;
		scsiio_request->SenseBufferLowAddress =
		    mpt2sas_base_get_sense_buffer_dma(ioc, smid);
		memset(ioc->ctl_cmds.sense, 0, SCSI_SENSE_BUFFERSIZE);
		if (mpi_request->Function == MPI2_FUNCTION_SCSI_IO_REQUEST)
			mpt2sas_base_put_smid_scsi_io(ioc, smid,
			    le16_to_cpu(mpi_request->FunctionDependent1));
		else
			mpt2sas_base_put_smid_default(ioc, smid);
		break;
	}
	case MPI2_FUNCTION_SCSI_TASK_MGMT:
	{
		Mpi2SCSITaskManagementRequest_t *tm_request =
		    (Mpi2SCSITaskManagementRequest_t *)request;

		dtmprintk(ioc, printk(MPT2SAS_INFO_FMT "TASK_MGMT: "
		    "handle(0x%04x), task_type(0x%02x)\n", ioc->name,
		    le16_to_cpu(tm_request->DevHandle), tm_request->TaskType));

		if (tm_request->TaskType ==
		    MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK ||
		    tm_request->TaskType ==
		    MPI2_SCSITASKMGMT_TASKTYPE_QUERY_TASK) {
			if (_ctl_set_task_mid(ioc, &karg, tm_request)) {
				mpt2sas_base_free_smid(ioc, smid);
				goto out;
			}
		}

		mpt2sas_scsih_set_tm_flag(ioc, le16_to_cpu(
		    tm_request->DevHandle));
		mpt2sas_base_put_smid_hi_priority(ioc, smid);
		break;
	}
	case MPI2_FUNCTION_SMP_PASSTHROUGH:
	{
		Mpi2SmpPassthroughRequest_t *smp_request =
		    (Mpi2SmpPassthroughRequest_t *)mpi_request;
		u8 *data;

		/* ioc determines which port to use */
		smp_request->PhysicalPort = 0xFF;
		if (smp_request->PassthroughFlags &
		    MPI2_SMP_PT_REQ_PT_FLAGS_IMMEDIATE)
			data = (u8 *)&smp_request->SGL;
		else
			data = data_out;

		if (data[1] == 0x91 && (data[10] == 1 || data[10] == 2)) {
			ioc->ioc_link_reset_in_progress = 1;
			ioc->ignore_loginfos = 1;
		}
		mpt2sas_base_put_smid_default(ioc, smid);
		break;
	}
	case MPI2_FUNCTION_SAS_IO_UNIT_CONTROL:
	{
		Mpi2SasIoUnitControlRequest_t *sasiounit_request =
		    (Mpi2SasIoUnitControlRequest_t *)mpi_request;

		if (sasiounit_request->Operation == MPI2_SAS_OP_PHY_HARD_RESET
		    || sasiounit_request->Operation ==
		    MPI2_SAS_OP_PHY_LINK_RESET) {
			ioc->ioc_link_reset_in_progress = 1;
			ioc->ignore_loginfos = 1;
		}
		mpt2sas_base_put_smid_default(ioc, smid);
		break;
	}
	default:
		mpt2sas_base_put_smid_default(ioc, smid);
		break;
	}

	if (karg.timeout < MPT2_IOCTL_DEFAULT_TIMEOUT)
		timeout = MPT2_IOCTL_DEFAULT_TIMEOUT;
	else
		timeout = karg.timeout;
	init_completion(&ioc->ctl_cmds.done);
	timeleft = wait_for_completion_timeout(&ioc->ctl_cmds.done,
	    timeout*HZ);
	if (mpi_request->Function == MPI2_FUNCTION_SCSI_TASK_MGMT) {
		Mpi2SCSITaskManagementRequest_t *tm_request =
		    (Mpi2SCSITaskManagementRequest_t *)mpi_request;
		mpt2sas_scsih_clear_tm_flag(ioc, le16_to_cpu(
		    tm_request->DevHandle));
	} else if ((mpi_request->Function == MPI2_FUNCTION_SMP_PASSTHROUGH ||
	    mpi_request->Function == MPI2_FUNCTION_SAS_IO_UNIT_CONTROL) &&
		ioc->ioc_link_reset_in_progress) {
		ioc->ioc_link_reset_in_progress = 0;
		ioc->ignore_loginfos = 0;
	}
	if (!(ioc->ctl_cmds.status & MPT2_CMD_COMPLETE)) {
		printk(MPT2SAS_ERR_FMT "%s: timeout\n", ioc->name,
		    __func__);
		_debug_dump_mf(mpi_request, karg.data_sge_offset);
		if (!(ioc->ctl_cmds.status & MPT2_CMD_RESET))
			issue_reset = 1;
		goto issue_host_reset;
	}

	mpi_reply = ioc->ctl_cmds.reply;
	ioc_status = le16_to_cpu(mpi_reply->IOCStatus) & MPI2_IOCSTATUS_MASK;

#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
	if (mpi_reply->Function == MPI2_FUNCTION_SCSI_TASK_MGMT &&
	    (ioc->logging_level & MPT_DEBUG_TM)) {
		Mpi2SCSITaskManagementReply_t *tm_reply =
		    (Mpi2SCSITaskManagementReply_t *)mpi_reply;

		printk(MPT2SAS_INFO_FMT "TASK_MGMT: "
		    "IOCStatus(0x%04x), IOCLogInfo(0x%08x), "
		    "TerminationCount(0x%08x)\n", ioc->name,
		    le16_to_cpu(tm_reply->IOCStatus),
		    le32_to_cpu(tm_reply->IOCLogInfo),
		    le32_to_cpu(tm_reply->TerminationCount));
	}
#endif
	/* copy out xdata to user */
	if (data_in_sz) {
		if (copy_to_user(karg.data_in_buf_ptr, data_in,
		    data_in_sz)) {
			printk(KERN_ERR "failure at %s:%d/%s()!\n", __FILE__,
			    __LINE__, __func__);
			ret = -ENODATA;
			goto out;
		}
	}

	/* copy out reply message frame to user */
	if (karg.max_reply_bytes) {
		sz = min_t(u32, karg.max_reply_bytes, ioc->reply_sz);
		if (copy_to_user(karg.reply_frame_buf_ptr, ioc->ctl_cmds.reply,
		    sz)) {
			printk(KERN_ERR "failure at %s:%d/%s()!\n", __FILE__,
			    __LINE__, __func__);
			ret = -ENODATA;
			goto out;
		}
	}

	/* copy out sense to user */
	if (karg.max_sense_bytes && (mpi_request->Function ==
	    MPI2_FUNCTION_SCSI_IO_REQUEST || mpi_request->Function ==
	    MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH)) {
		sz = min_t(u32, karg.max_sense_bytes, SCSI_SENSE_BUFFERSIZE);
		if (copy_to_user(karg.sense_data_ptr,
			ioc->ctl_cmds.sense, sz)) {
			printk(KERN_ERR "failure at %s:%d/%s()!\n", __FILE__,
			    __LINE__, __func__);
			ret = -ENODATA;
			goto out;
		}
	}

 issue_host_reset:
	if (issue_reset) {
		ret = -ENODATA;
		if ((mpi_request->Function == MPI2_FUNCTION_SCSI_IO_REQUEST ||
		    mpi_request->Function ==
		    MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH)) {
			printk(MPT2SAS_INFO_FMT "issue target reset: handle "
			    "= (0x%04x)\n", ioc->name,
			    le16_to_cpu(mpi_request->FunctionDependent1));
			mpt2sas_halt_firmware(ioc);
			mpt2sas_scsih_issue_tm(ioc,
			    le16_to_cpu(mpi_request->FunctionDependent1), 0, 0,
			    0, MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET, 0, 10,
			    0, TM_MUTEX_ON);
			ioc->tm_cmds.status = MPT2_CMD_NOT_USED;
		} else
			mpt2sas_base_hard_reset_handler(ioc, CAN_SLEEP,
			    FORCE_BIG_HAMMER);
	}
#endif
 out:

	/* free memory associated with sg buffers */
	if (data_in)
		pci_free_consistent(ioc->pdev, data_in_sz, data_in,
		    data_in_dma);

	if (data_out)
		pci_free_consistent(ioc->pdev, data_out_sz, data_out,
		    data_out_dma);

	kfree(mpi_request);
	ioc->ctl_cmds.status = MPT2_CMD_NOT_USED;
	mutex_unlock(&ioc->ctl_cmds.mutex);
	return ret;
}

/**
 * _ctl_verify_adapter - validates ioc_number passed from application
 * @ioc: per adapter object
 * @iocpp: The ioc pointer is returned in this.
 *
 * Return (-1) means error, else ioc_number.
 */
static int
_ctl_verify_adapter(int ioc_number, struct MPT2SAS_ADAPTER **iocpp)
{
	struct MPT2SAS_ADAPTER *ioc;

	list_for_each_entry(ioc, &mpt2sas_ioc_list, list) {
		if (ioc->id != ioc_number)
			continue;
		*iocpp = ioc;
		return ioc_number;
	}
	*iocpp = NULL;
	return -1;
}

/**
 * _ctl_diag_capability - return diag buffer capability
 * @ioc: per adapter object
 * @buffer_type: specifies either TRACE, SNAPSHOT, or EXTENDED
 *
 * returns 1 when diag buffer support is enabled in firmware
 */
static u8
_ctl_diag_capability(struct MPT2SAS_ADAPTER *ioc, u8 buffer_type)
{
	u8 rc = 0;

	switch (buffer_type) {
	case MPI2_DIAG_BUF_TYPE_TRACE:
		if (ioc->facts.IOCCapabilities &
		    MPI2_IOCFACTS_CAPABILITY_DIAG_TRACE_BUFFER)
			rc = 1;
		break;
	case MPI2_DIAG_BUF_TYPE_SNAPSHOT:
		if (ioc->facts.IOCCapabilities &
		    MPI2_IOCFACTS_CAPABILITY_SNAPSHOT_BUFFER)
			rc = 1;
		break;
	case MPI2_DIAG_BUF_TYPE_EXTENDED:
		if (ioc->facts.IOCCapabilities &
		    MPI2_IOCFACTS_CAPABILITY_EXTENDED_BUFFER)
			rc = 1;
	}

	return rc;
}

#if 0
/**
 * _ctl_diag_read_buffer - request for copy of the diag buffer
 * @arg - user space buffer containing ioctl content
 * @state - NON_BLOCKING or BLOCKING
 */
/*static*/ long
_ctl_diag_read_buffer(void __user *arg, enum block_state state)
{
	struct mpt2_diag_read_buffer karg;
	struct mpt2_diag_read_buffer __user *uarg = arg;
	struct MPT2SAS_ADAPTER *ioc;
	void *request_data, *diag_data;
	Mpi2DiagBufferPostRequest_t *mpi_request;
	Mpi2DiagBufferPostReply_t *mpi_reply;
	int rc, i;
	u8 buffer_type;
#ifdef __PATCH__
	unsigned long timeleft, request_size, copy_size;
#else
	unsigned long timeleft;
#endif
	u16 smid;
	u16 ioc_status;
	u8 issue_reset = 0;

	if (copy_from_user(&karg, arg, sizeof(karg))) {
		printk(KERN_ERR "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -EFAULT;
	}
	if (_ctl_verify_adapter(karg.hdr.ioc_number, &ioc) == -1 || !ioc)
		return -ENODEV;

	dctlprintk(ioc, printk(MPT2SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	buffer_type = karg.unique_id & 0x000000ff;
	if (!_ctl_diag_capability(ioc, buffer_type)) {
		printk(MPT2SAS_ERR_FMT "%s: doesn't have capability for "
		    "buffer_type(0x%02x)\n", ioc->name, __func__, buffer_type);
		return -EPERM;
	}

	if (karg.unique_id != ioc->unique_id[buffer_type]) {
		printk(MPT2SAS_ERR_FMT "%s: unique_id(0x%08x) is not "
		    "registered\n", ioc->name, __func__, karg.unique_id);
		return -EINVAL;
	}

	request_data = ioc->diag_buffer[buffer_type];
	if (!request_data) {
		printk(MPT2SAS_ERR_FMT "%s: doesn't have buffer for "
		    "buffer_type(0x%02x)\n", ioc->name, __func__, buffer_type);
		return -ENOMEM;
	}
#ifdef __PATCH__
	request_size = ioc->diag_buffer_sz[buffer_type];
#endif

	if ((karg.starting_offset % 4) || (karg.bytes_to_read % 4)) {
		printk(MPT2SAS_ERR_FMT "%s: either the starting_offset "
		    "or bytes_to_read are not 4 byte aligned\n", ioc->name,
		    __func__);
		return -EINVAL;
	}
#ifdef __PATCH__
	if (karg.starting_offset > request_size)
		return -EINVAL;
#endif
	diag_data = (void *)(request_data + karg.starting_offset);
	dctlprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: diag_buffer(%p), "
	    "offset(%d), sz(%d)\n", ioc->name, __func__,
	    diag_data, karg.starting_offset, karg.bytes_to_read));
#ifdef __PATCH__
	/* Truncate data on requests that are too large */
	if ((diag_data + karg.bytes_to_read < diag_data) ||
	    (diag_data + karg.bytes_to_read > request_data + request_size))
		copy_size = request_size - karg.starting_offset;
	else
		copy_size = karg.bytes_to_read;
#endif
	if (copy_to_user((void __user *)uarg->diagnostic_data,
#ifdef __PATCH__
	    diag_data, copy_size)) {
#else
		diag_data, karg.bytes_to_read)) {
#endif
		printk(MPT2SAS_ERR_FMT "%s: Unable to write "
		    "mpt_diag_read_buffer_t data @ %p\n", ioc->name,
		    __func__, diag_data);
		return -EFAULT;
	}

	if ((karg.flags & MPT2_FLAGS_REREGISTER) == 0)
		return 0;

	dctlprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: Reregister "
		"buffer_type(0x%02x)\n", ioc->name, __func__, buffer_type));
	if ((ioc->diag_buffer_status[buffer_type] &
	    MPT2_DIAG_BUFFER_IS_RELEASED) == 0) {
		dctlprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: "
		    "buffer_type(0x%02x) is still registered\n", ioc->name,
		     __func__, buffer_type));
		return 0;
	}
	/* Get a free request frame and save the message context.
	*/
	if (state == NON_BLOCKING && !mutex_trylock(&ioc->ctl_cmds.mutex))
		return -EAGAIN;
	else if (mutex_lock_interruptible(&ioc->ctl_cmds.mutex))
		return -ERESTARTSYS;

	if (ioc->ctl_cmds.status != MPT2_CMD_NOT_USED) {
		printk(MPT2SAS_ERR_FMT "%s: ctl_cmd in use\n",
		    ioc->name, __func__);
		rc = -EAGAIN;
		goto out;
	}

	smid = mpt2sas_base_get_smid(ioc, ioc->ctl_cb_idx);
	if (!smid) {
		printk(MPT2SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		rc = -EAGAIN;
		goto out;
	}

	rc = 0;
	ioc->ctl_cmds.status = MPT2_CMD_PENDING;
	memset(ioc->ctl_cmds.reply, 0, ioc->reply_sz);
	mpi_request = mpt2sas_base_get_msg_frame(ioc, smid);
	ioc->ctl_cmds.smid = smid;

	mpi_request->Function = MPI2_FUNCTION_DIAG_BUFFER_POST;
	mpi_request->BufferType = buffer_type;
	mpi_request->BufferLength =
	    cpu_to_le32(ioc->diag_buffer_sz[buffer_type]);
	mpi_request->BufferAddress =
	    cpu_to_le64(ioc->diag_buffer_dma[buffer_type]);
	for (i = 0; i < MPT2_PRODUCT_SPECIFIC_DWORDS; i++)
		mpi_request->ProductSpecific[i] =
			cpu_to_le32(ioc->product_specific[buffer_type][i]);
	mpi_request->VF_ID = 0; /* TODO */
	mpi_request->VP_ID = 0;

	mpt2sas_base_put_smid_default(ioc, smid);
	init_completion(&ioc->ctl_cmds.done);
	timeleft = wait_for_completion_timeout(&ioc->ctl_cmds.done,
	    MPT2_IOCTL_DEFAULT_TIMEOUT*HZ);

	if (!(ioc->ctl_cmds.status & MPT2_CMD_COMPLETE)) {
		printk(MPT2SAS_ERR_FMT "%s: timeout\n", ioc->name,
		    __func__);
		_debug_dump_mf(mpi_request,
		    sizeof(Mpi2DiagBufferPostRequest_t)/4);
		if (!(ioc->ctl_cmds.status & MPT2_CMD_RESET))
			issue_reset = 1;
		goto issue_host_reset;
	}

	/* process the completed Reply Message Frame */
	if ((ioc->ctl_cmds.status & MPT2_CMD_REPLY_VALID) == 0) {
		printk(MPT2SAS_ERR_FMT "%s: no reply message\n",
		    ioc->name, __func__);
		rc = -EFAULT;
		goto out;
	}

	mpi_reply = ioc->ctl_cmds.reply;
	ioc_status = le16_to_cpu(mpi_reply->IOCStatus) & MPI2_IOCSTATUS_MASK;

	if (ioc_status == MPI2_IOCSTATUS_SUCCESS) {
		ioc->diag_buffer_status[buffer_type] |=
		    MPT2_DIAG_BUFFER_IS_REGISTERED;
		dctlprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: success\n",
		    ioc->name, __func__));
	} else {
		printk(MPT2SAS_INFO_FMT "%s: ioc_status(0x%04x) "
		    "log_info(0x%08x)\n", ioc->name, __func__,
		    ioc_status, le32_to_cpu(mpi_reply->IOCLogInfo));
		rc = -EFAULT;
	}

 issue_host_reset:
	if (issue_reset)
		mpt2sas_base_hard_reset_handler(ioc, CAN_SLEEP,
		    FORCE_BIG_HAMMER);

 out:

	ioc->ctl_cmds.status = MPT2_CMD_NOT_USED;
	mutex_unlock(&ioc->ctl_cmds.mutex);
	return rc;
}
#endif
