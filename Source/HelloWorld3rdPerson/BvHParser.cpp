#include "BvHParser.h"

#include "Logging/LogMacros.h"

#include <ios>
#include <sstream>
#include <string>
#include <fstream>
#include <iterator>

/** Indicate whether bvh parser allows multi hierarchy or not
  * Not fully tested
  */
#define MULTI_HIERARCHY 0

namespace {

const std::string kChannels = "CHANNELS";
const std::string kEnd = "End";
const std::string kEndSite = "End Site";
const std::string kFrame = "Frame";
const std::string kFrames = "Frames:";
const std::string kHierarchy = "HIERARCHY";
const std::string kJoint = "JOINT";
const std::string kMotion = "MOTION";
const std::string kOffset = "OFFSET";
const std::string kRoot = "ROOT";

const std::string kXpos = "Xposition";
const std::string kYpos = "Yposition";
const std::string kZpos = "Zposition";
const std::string kXrot = "Xrotation";
const std::string kYrot = "Yrotation";
const std::string kZrot = "Zrotation";

}

namespace bvh {

//##############################################################################
// Main parse function
//##############################################################################
int BvHParser::parse(const FString& path, BvHSkeleton* bvh) {
  UE_LOG(LogTemp, Display, TEXT("Parsing file : %s"), *path); 

  path_ = path;
  bvh_ = bvh;

  std::ifstream file;
  std::string pathstr(TCHAR_TO_UTF8(*path_));
  file.open(pathstr);

  if (file.is_open()) {
    std::string token;

#if MULTI_HIERARCHY == 1
    while (file.good()) {
#endif
      file >> token;
      if (token == kHierarchy) {
        int ret = parse_hierarchy(file);
        if (ret)
          return ret;
      } else {
        UE_LOG(LogTemp, Display, TEXT("Bad structure of .bvh file. %s should be on the top of the file"), *FString(kHierarchy.c_str())); 
        return -1;
      }
#if MULTI_HIERARCHY == 1
    }
#endif
  } else {
    UE_LOG(LogTemp, Display, TEXT("Cannot open file to parse  : %s"), *path_); 
    return -1;
  }

  UE_LOG(LogTemp, Display, TEXT("Successfully parsed file")); 
  return 0;
}

int BvHParser::parse_hierarchy(std::ifstream& file) {
  UE_LOG(LogTemp, Display, TEXT("Parsing hierarchy")); 

  std::string token;
  int ret;

  if (file.good()) {
    file >> token;

    //##########################################################################
    // Parsing joints
    //##########################################################################
    if (token == kRoot) {
      std::shared_ptr <Joint> rootJoint;
      ret = parse_joint(file, nullptr, rootJoint);

      if (ret)
        return ret;

      UE_LOG(LogTemp, Display, TEXT("There is %d data channels in the file"), bvh_->num_channels()); 

      bvh_->set_root_joint(rootJoint);
    } else {
        UE_LOG(LogTemp, Display, TEXT("Bad structure of .bvh file. Expected %s, but found %s"), *FString(kRoot.c_str()), *FString(token.c_str())); 
      return -1;
    }
  }

  if (file.good()) {
    file >> token;

    //##########################################################################
    // Parsing motion data
    //##########################################################################
    if (token == kMotion) {
      ret = parse_motion(file);

      if (ret)
        return ret;
    } else {
        UE_LOG(LogTemp, Display, TEXT("Bad structure of .bvh file. Expected %s, but found %s"), *FString(kMotion.c_str()), *FString(token.c_str())); 
      return -1;
    }
  }
  return 0;
}

int BvHParser::parse_joint(std::ifstream& file,
    std::shared_ptr <Joint> parent, std::shared_ptr <Joint>& parsed) {

  UE_LOG(LogTemp, Display, TEXT("Parsing joint")); 

  std::shared_ptr<Joint> joint = std::make_shared<Joint>();
  joint->set_parent(parent);

  std::string name;
  file >> name;

  UE_LOG(LogTemp, Display, TEXT("Joint name : %s"), *FString(name.c_str())); 

  joint->set_name(name);

  std::string token;
  std::vector <std::shared_ptr <Joint>> children;
  int ret;

  file >> token;  // Consuming '{'
  file >> token;

  //############################################################################
  // Offset parsing
  //############################################################################
  if (token == kOffset) {
    Joint::Offset offset;

    try {
      file >> offset.x >> offset.y >> offset.z;
    } catch (const std::ios_base::failure e) {
    //   LOG(ERROR) << "Failure while parsing offset";
      return -1;
    }

    joint->set_offset(offset);

    // LOG(TRACE) << "Offset x: " << offset.x << ", y: " << offset.y << ", z: "
    //            << offset.z;

  } else {
    // LOG(ERROR) << "Bad structure of .bvh file. Expected " << kOffset << ", but "
    //            << "found \"" << token << "\"";

    return -1;
  }

  file >> token;

  //############################################################################
  // Channels parsing
  //############################################################################
  if (token == kChannels) {
    ret = parse_channel_order(file, joint);

    // LOG(TRACE) << "Joint has " << joint->num_channels() << " data channels";

    if (ret)
      return ret;
  } else {
    // LOG(ERROR) << "Bad structure of .bvh file. Expected " << kChannels
    //            << ", but found \"" << token << "\"";

    return -1;
  }

  file >> token;

  bvh_->add_joint(joint);

  //############################################################################
  // Children parsing
  //############################################################################

  while (file.good()) {
    //##########################################################################
    // Child joint parsing
    //##########################################################################
    if (token == kJoint) {
      std::shared_ptr <Joint> child;
      ret = parse_joint(file, joint, child);

      if (ret)
        return ret;

      children.push_back(child);

    //##########################################################################
    // Child joint parsing
    //##########################################################################
    } else if (token == kEnd) {
      file >> token >> token;  // Consuming "Site {"

      std::shared_ptr <Joint> tmp_joint = std::make_shared <Joint> ();

      tmp_joint->set_parent(joint);
      tmp_joint->set_name(kEndSite);
      children.push_back(tmp_joint);

      file >> token;

      //########################################################################
      // End site offset parsing
      //########################################################################
      if (token == kOffset) {
        Joint::Offset offset;

        try {
          file >> offset.x >> offset.y >> offset.z;
        } catch (const std::ios_base::failure e) {
        //   LOG(ERROR) << "Failure while parsing offset";
          return -1;
        }

        tmp_joint->set_offset(offset);

        // LOG(TRACE) << "Joint name : EndSite";
        // LOG(TRACE) << "Offset x: " << offset.x << ", y: " << offset.y << ", z: "
        //            << offset.z;

        file >> token;  // Consuming "}"

      } else {
        // LOG(ERROR) << "Bad structure of .bvh file. Expected " << kOffset
        //            << ", but found \"" << token << "\"";

        return -1;
      }

      bvh_->add_joint(tmp_joint);
    //##########################################################################
    // End joint parsing
    //##########################################################################
    } else if (token == "}") {
      joint->set_children(children);
      parsed = joint;
      return 0;
    }

    file >> token;
  }

//   LOG(ERROR) << "Cannot parse joint, unexpected end of file. Last token : "
//              << token;
  return -1;
}

//##############################################################################
// Motion data parse function
//##############################################################################
int BvHParser::parse_motion(std::ifstream& file) {

//   LOG(INFO) << "Parsing motion";

  std::string token;
  file >> token;

  int frames_num;

  if (token == kFrames) {
    file >> frames_num;
    bvh_->set_num_frames(frames_num);
    // LOG(INFO) << "Num of frames : " << frames_num;
  } else {
    // LOG(ERROR) << "Bad structure of .bvh file. Expected " << kFrames
    //            << ", but found \"" << token << "\"";

    return -1;
  }

  file >> token;

  double frame_time;

  if (token == kFrame) {
    file >> token;  // Consuming 'Time:'
    file >> frame_time;
    bvh_->set_frame_time(frame_time);
    // LOG(INFO) << "Frame time : " << frame_time;

    float number;
    for (int i = 0; i < frames_num; i++) {
      for (auto joint : bvh_->joints()) {
        std::vector <float> data;
        for (unsigned j = 0; j < joint->num_channels(); j++) {
          file >> number;
          data.push_back(number);
        }
        // LOG(TRACE) << joint->name() << ": " << vtos(data);
        joint->add_frame_motion_data(data);
      }
    }
  } else {
    // LOG(ERROR) << "Bad structure of .bvh file. Expected " << kFrame
    //            << ", but found \"" << token << "\"";

    return -1;
  }

  return 0;
}

//##############################################################################
// Channels order parse function
//##############################################################################
int BvHParser::parse_channel_order(std::ifstream& file,
    std::shared_ptr <Joint> joint) {

//   LOG(TRACE) << "Parse channel order";

  int num;
  file >> num;
//   LOG(TRACE) << "Number of channels : " << num;

  std::vector <Joint::Channel> channels;
  std::string token;

  for (int i = 0; i < num; i++) {
    file >> token;
    if (token == kXpos)
      channels.push_back(Joint::Channel::XPOSITION);
    else if (token == kYpos)
      channels.push_back(Joint::Channel::YPOSITION);
    else if (token == kZpos)
      channels.push_back(Joint::Channel::ZPOSITION);
    else if (token == kXrot)
      channels.push_back(Joint::Channel::XROTATION);
    else if (token == kYrot)
      channels.push_back(Joint::Channel::YROTATION);
    else if (token == kZrot)
      channels.push_back(Joint::Channel::ZROTATION);
    else {
    //   LOG(ERROR) << "Not valid channel!";
      return -1;
    }
  }

  joint->set_channels_order(channels);
  return 0;
}

std::string BvHParser::vtos(const std::vector <float>& vector) {
  std::ostringstream oss;

  if (!vector.empty())
  {
    // Convert all but the last element to avoid a trailing ","
    std::copy(vector.begin(), vector.end()-1,
        std::ostream_iterator<float>(oss, ", "));

    // Now add the last element with no delimiter
    oss << vector.back();
  }

  return oss.str();
}

} // namespace