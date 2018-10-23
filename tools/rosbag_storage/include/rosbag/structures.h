/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2008, Willow Garage, Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

#ifndef ROSBAG_STRUCTURES_H
#define ROSBAG_STRUCTURES_H

#include <map>
#include <vector>

#include "ros/time.h"
#include "ros/datatypes.h"
#include "macros.h"

namespace rosbag {


//与bag建立起来的每个连接都有对应的属性
struct ROSBAG_STORAGE_DECL ConnectionInfo
{
    ConnectionInfo() : id(-1) { }

    uint32_t    id;//每个建立起来的connsection都存在一个id
    std::string topic;//topic名称
    std::string datatype;//数据类型
    std::string md5sum;//md5值
    std::string msg_def;//消息定义

    boost::shared_ptr<ros::M_string> header;//header map
};

//每个chunk的属性，包括内部存储消息的最小时间和最大时间，以及这个chunk在文件中的偏移
struct ChunkInfo
{
    ros::Time   start_time;    //! earliest timestamp of a message in the chunk，在chunk中的最早的时间
    ros::Time   end_time;      //! latest timestamp of a message in the chunk，在chunk中最晚的时间
    uint64_t    pos;           //! absolute byte offset of chunk record in bag file，chunk在bag中的绝对偏移

    std::map<uint32_t, uint32_t> connection_counts;   //! number of messages in each connection stored in the chunk，在这个chunk中每个connection中消息的数量
};

struct ROSBAG_STORAGE_DECL ChunkHeader
{
    std::string compression;          //! chunk compression type, e.g. "none" or "bz2" (see constants.h)，压缩类型
    uint32_t    compressed_size;      //! compressed size of the chunk in bytes，压缩时chunk的大小
    uint32_t    uncompressed_size;    //! uncompressed size of the chunk in bytes，未压缩时chunk的大小
};

//描述了消息的属性，时间、所在chunk的位置、在chunk中的偏移
struct ROSBAG_STORAGE_DECL IndexEntry
{
    ros::Time time;            //! timestamp of the message，消息的时间戳
    uint64_t  chunk_pos;       //! absolute byte offset of the chunk record containing the message，这条消息所在chunk在文件中的偏移量
    uint32_t  offset;          //! relative byte offset of the message record (either definition or data) in the chunk，在chunk中这个消息的偏移量

    bool operator<(IndexEntry const& b) const { return time < b.time; }//提供一个<符号重载，这个对于set排序来说很关键
};

struct ROSBAG_STORAGE_DECL IndexEntryCompare
{
    bool operator()(ros::Time const& a, IndexEntry const& b) const { return a < b.time; }
    bool operator()(IndexEntry const& a, ros::Time const& b) const { return a.time < b; }
};

} // namespace rosbag

#endif
