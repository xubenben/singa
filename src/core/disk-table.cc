#include "core/disk-table.h"
#include "core/file.h"
#include "utils/network_thread.h"
#include "utils/global_context.h"
#include <gflags/gflags.h>

DEFINE_string(data_dir,"tmp", "path to data store");
DEFINE_int32(table_buffer, 1,0);
DEFINE_int32(io_buffer_size,5,0);
DECLARE_double(sleep_time);

namespace lapis{

//  iterator through disk file
DiskTableIterator::DiskTableIterator(const string name, DiskData* msg): file_(name, "r"), done_(true)
										, data_(NULL){
	file_.seek(0);
	data_ = msg;
	Next();
}

DiskTableIterator::~DiskTableIterator(){
	//delete &file_;
	delete data_;
}

void DiskTableIterator::Next(){
	VLOG(3)<< "before read file " << file_.name() << " position " << file_.fp->tell();
	done_ = !file_.read(data_);
	VLOG(3)<< "after read file " << file_.name() << " position " << file_.fp->tell()<< " done = " << done_	;

}

bool DiskTableIterator::done(){ return done_;}

DiskData* DiskTableIterator::value(){ return data_;}

void DiskTable::Load(){
	//  get all files on the first load.
	//  on re-load, simply reset the pointer
  VLOG(3)<<"disktable loading";
	if (blocks_.empty()) {
		vector<File::Info> files = File::MatchingFileinfo(
				StringPrintf("%s/%s_*", FLAGS_data_dir.c_str(),
						table_info_->name_prefix.c_str()));
		VLOG(3) << "Searched for file with prefix " << table_info_->name_prefix << "from dir " << FLAGS_data_dir;
		for (size_t i = 0; i < files.size(); i++) {
			VLOG(3) << "Found file "<< files[i].name;
			FileBlock *block = new FileBlock();
			block->info = files[i];
			block->end_pos = files[i].stat.st_size;
			blocks_.push_back(block);
		}
	}
  // starting the IO thread
	buffer_.reset(new PrefetchedBuffer((int)FLAGS_io_buffer_size));
	read_thread_.reset(new boost::thread(&DiskTable::read_loop, this));

	//  wait until we load the first DiskData
	while (buffer_->empty())
		Sleep(FLAGS_sleep_time);
	current_read_record_.reset(buffer_->next_data_records());
	current_idx_ = 0;
  has_loaded_=true;
  VLOG(3) << "Got the first record " << current_read_record_;
}

void DiskTable::DumpToFile(const DiskData* data){
	if (!file_)
		file_ = new RecordFile(
				StringPrintf("%s/%s_%d", FLAGS_data_dir.c_str(),
						table_info_->name_prefix.c_str(), data->block_number()),
				"w");

	VLOG(3) << "DUMPING to file " << file_->name();
	if ((int)(data->block_number())!=current_block_){
		delete file_;
		file_ = new RecordFile(
				StringPrintf("%s/%s_%d", FLAGS_data_dir.c_str(),
						table_info_->name_prefix.c_str(), data->block_number()),
				"w");
		current_block_ = data->block_number();
	}
	file_->write(*data);

}

void DiskTable::put_str(const string& k, const string& v){
	if (!current_write_record_){ // first time
		// starting write IO thread
		buffer_.reset(new PrefetchedBuffer((int)FLAGS_io_buffer_size));
		write_thread_.reset(new boost::thread(&DiskTable::write_loop, this));

		current_write_record_ = new DiskData();
		current_write_record_->set_block_number(current_block_);
		current_write_record_->set_table(id());
		VLOG(3) << "Initialized write IO buffer";
	}

	//  serialize to disk
	Arg* new_record = current_write_record_->add_records();
	new_record->set_key(k.c_str(), k.length());
	new_record->set_value(v.c_str(), v.length());
	current_buffer_count_++;
	total_buffer_count_++;

	if (current_buffer_count_ >= FLAGS_table_buffer) {
		VLOG(3) << "Try adding to IO write buffer ";
		while (!(buffer_->add_data_records(current_write_record_)))
			Sleep (FLAGS_sleep_time);
		VLOG(3) << "Added to IO write buffer ";
		current_write_record_ = new DiskData();
		if (total_buffer_count_ >= table_info_->max_size) {
			current_block_++;
			total_buffer_count_ = 0;
		}
		current_write_record_->set_block_number(current_block_);
		current_write_record_->set_table(id());
		current_buffer_count_ = 0;
	}
}

void DiskTable::get_str(string *k, string *v){
	k->assign((current_read_record_->records(current_idx_)).key());

	VLOG(3) << "getting str, current record = " << current_read_record_
				<< " value data size = "
				<< current_read_record_->records(current_idx_).value().size();

	v->assign((current_read_record_->records(current_idx_)).value());
	VLOG(3) << "after assigning to string, length = "<< v->size() << " current idx = "<<current_idx_;
}

//  flush the current buffer
void DiskTable::finish_put(){

	//  done, flush the buffer
	done_writing_ = true;
	while (!buffer_->empty()){
		SendDataBuffer(*(buffer_->next_data_records()));
	}

	VLOG(3) << "Flushed all data put";
	//  wait for other to confirm that data has been stored
	NetworkThread::Get()->SyncBroadcast(MTYPE_DATA_PUT_REQUEST_FINISH,
							MTYPE_DATA_PUT_REQUEST_DONE, EmptyMessage());
}

//  reach the last record of the last file
bool DiskTable::done(){
	return current_iterator_->done() && current_block_>=(int)(blocks_.size()) && buffer_->empty();
}


void DiskTable::read_loop(){
	//  point the current iterator to the first file
	current_block_ = 0;
	current_iterator_.reset(new DiskTableIterator(
			(blocks_[current_block_]->info).name, new DiskData()));
	current_block_++;

	//  more blocks to add
	while (!current_iterator_->done() || current_block_ < (int) (blocks_.size())) {
		//VLOG(3) << "Trying to add data ++++";
		while (!(buffer_->add_data_records(current_iterator_->value())))
			Sleep (FLAGS_sleep_time);
		current_iterator_->Next();

		VLOG(3) << "current iterator moved to NEXT";
		//  if end of file, move to next one
		if (current_iterator_->done()) {
			VLOG(3) << " end of file, NEXT BLOCK, current block = "
					<< current_block_ << " vs. size = " << blocks_.size();
			if (current_block_ < (int) (blocks_.size())) {
				current_iterator_.reset(
						new DiskTableIterator(
								(blocks_[current_block_]->info).name,
								new DiskData()));
				current_block_++;
			}
		}
	}

	VLOG(3) << "read thread stops ....";
}

void DiskTable::write_loop(){
	while (!done_writing_){
		DiskData* data;
		//VLOG(3) << "Getting data to send to server, buffer is empty  " << buffer_->empty();
		while (!(data=buffer_->next_data_records())){
			Sleep(FLAGS_sleep_time);
		}
		//VLOG(3) << "Sending data block to server ";
		SendDataBuffer(*data);

	}

	VLOG(3) << "write thread stops ....";
}

// getting next value. Iterate through DiskData table and through the file as well
void DiskTable::Next(){

	current_idx_++;
	if (current_idx_==current_read_record_->records_size()){
		DiskData* data;
		while (!(data = buffer_->next_data_records()))
				Sleep(FLAGS_sleep_time);

		current_read_record_.reset(data);
		current_idx_=0;
	}
}

void DiskTable::SendDataBuffer(const DiskData& data){
	int dest = table_info_->fixed_server_id;
	if (dest==-1)
		dest = data.block_number()%(GlobalContext::Get()->num_table_servers());

	NetworkThread::Get()->Send(dest,MTYPE_DATA_PUT_REQUEST, data);
}

DiskTable::~DiskTable(){
	delete table_info_;

	for (size_t i=0; i<blocks_.size(); i++)
		delete blocks_[i];

	//delete current_iterator_;
	delete current_write_record_;
}

bool PrefetchedBuffer::empty(){
	boost::recursive_mutex::scoped_lock sl(data_queue_lock_);
	return data_queue_.size()==0;
}

bool PrefetchedBuffer::add_data_records(DiskData* data){
	boost::recursive_mutex::scoped_lock sl(data_queue_lock_);
	if ((int)(data_queue_.size())<max_size_){
		DiskData* copyData = new DiskData(*data);
		data_queue_.push_back(copyData);
		VLOG(3) << "Data " << copyData << " PUSHED to queue, front = " << data_queue_.front();
		return true;
	}
	return false;
}

DiskData* PrefetchedBuffer::next_data_records(){
	boost::recursive_mutex::scoped_lock sl(data_queue_lock_);
	if (data_queue_.size()>0 && (int)(data_queue_.size())<=max_size_){
		DiskData* data = data_queue_.front();
		data_queue_.pop_front();
		VLOG(3) << "After poping from queue, data return = " << data;
		return data;
	}
	else{
		return NULL;
	}
}
}
