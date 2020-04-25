
#include "Kalman.h"

using namespace std;
int xx_min = -99;
int xx_max = 99;


class Tracker{
public:
	Tracker();
	bool polynomial_curve_fit(std::vector<cv::Point>& key_point, int n, cv::Mat& A);
	void Dis_clustering(const pcl::PointCloud<pcl::PointXYZI> in_cloud,std::vector<pcl::PointIndices> &cluster_indices);
	void tracking(const pcl::PointCloud<pcl::PointXYZI>::Ptr in_cloud,std::vector<pcl::PointIndices> &cluster_indices,const pcl::PointCloud<pcl::PointXYZ>::Ptr predict_cloud,const pcl::PointCloud<pcl::PointXYZ>::Ptr estimated_cloud);
	void processPointcloud(const sensor_msgs::PointCloud2 &scan);
	void init_kalman(cv::KalmanFilter &kalman, const Target_ tar_temp);
	void getClusterVertex(std::vector<cv::Point> &cluster, Vertex &tmp);
private:
	ros::NodeHandle nh;
	ros::Subscriber points_sub;
	ros::Publisher predict_pub;
	ros::Publisher estimate_pub;
	vector<Target_> tar_list;
	bool init;
	int track_num;
};

Tracker::Tracker()
{
	tar_list.clear();
	init = false;
	track_num = 0;
	points_sub = nh.subscribe("velodyne_points", 1028, &Tracker::processPointcloud, this);
	predict_pub = nh.advertise<sensor_msgs::PointCloud2>("/predict_points", 10);
	estimate_pub = nh.advertise<sensor_msgs::PointCloud2>("/estimated_points", 10);
}


void Tracker::processPointcloud(const sensor_msgs::PointCloud2 &scan) {
	pcl::PCLPointCloud2 pcl_pc;
	pcl_conversions::toPCL(scan,pcl_pc);
	pcl::PointCloud<pcl::PointXYZI>::Ptr temp_cloud(new pcl::PointCloud<pcl::PointXYZI>);
	pcl::fromPCLPointCloud2(pcl_pc,*temp_cloud); // all points' data are stored in temp_cloud

	std::vector<pcl::PointIndices> cluster_indices;
	Dis_clustering(*temp_cloud,cluster_indices);

	pcl::PointCloud<pcl::PointXYZ>::Ptr predict_cloud(new pcl::PointCloud<pcl::PointXYZ>);
	pcl::PointCloud<pcl::PointXYZ>::Ptr estimated_cloud(new pcl::PointCloud<pcl::PointXYZ>);
	for (int i = 0; i < tar_list.size(); ++i) {
		tar_list[i].match = 0;
	}

	tracking(temp_cloud,cluster_indices,predict_cloud,estimated_cloud);

	sensor_msgs::PointCloud2 pub_predict;
	sensor_msgs::PointCloud2 pub_estimated;

	if(predict_cloud->points.size()!=0)
	{
		pcl::toROSMsg(*predict_cloud, pub_predict);
		pub_predict.header.frame_id = "velodyne";
		predict_pub.publish(pub_predict);
	}
	if(estimated_cloud->points.size()!=0)
	{
		pcl::toROSMsg(*estimated_cloud, pub_estimated);
		pub_estimated.header.frame_id = "velodyne";
		estimate_pub.publish(pub_estimated);
	}
	init = true;

}


void lidarPointsFilter(std::vector<cv::Point3d>& points,std::vector<cv::Point3d>& filter_points)
{
	for(auto it : points){
		if(it.y < 50 && it.x <50 && it.y > -50 && it.x > -50)
			filter_points.push_back(it);
	}
}

//最小二乘法求系数矩阵
bool Tracker::polynomial_curve_fit(std::vector<cv::Point>& key_point, int n, cv::Mat& A)
{
	//Number of key points
	int N = key_point.size();
 
	//构造矩阵X
	cv::Mat X = cv::Mat::zeros(n + 1, n + 1, CV_64FC1);
	for (int i = 0; i < n + 1; i++)
	{
		for (int j = 0; j < n + 1; j++)
		{
			for (int k = 0; k < N; k++)
			{
				X.at<double>(i, j) = X.at<double>(i, j) +
					std::pow(key_point[k].x, i + j);
			}
		}
	}
 
	//构造矩阵Y
	cv::Mat Y = cv::Mat::zeros(n + 1, 1, CV_64FC1);
	for (int i = 0; i < n + 1; i++)
	{
		for (int k = 0; k < N; k++)
		{
			Y.at<double>(i, 0) = Y.at<double>(i, 0) +
				std::pow(key_point[k].x, i) * key_point[k].y;
		}
	}
 
	A = cv::Mat::zeros(n + 1, 1, CV_64FC1);
	//求解矩阵A
	cv::solve(X, Y, A, cv::DECOMP_LU);
	return true;
}


void Tracker::getClusterVertex(std::vector<cv::Point> &cluster, Vertex &tmp)
{
	float maxx(-9999),minx(9999),maxy(-9999),miny(9999); // upperVertex (maxx,maxy), lowerVertex (minx,miny)
	float y_max(0),y_min(0);
	float x_max(0),x_min(0);
	
	for (int i = 0; i < cluster.size(); ++i) {
		if(cluster[i].x > maxx){
			maxx = cluster[i].x;
			y_max = cluster[i].y;
		}
		if(cluster[i].y > maxy){
			maxy = cluster[i].y;
			x_max = cluster[i].x;
		}
		if(cluster[i].x < minx){
			minx = cluster[i].x;
			y_min = cluster[i].y;
		}
		if(cluster[i].y < miny){
			miny = cluster[i].y;	
			x_min = cluster[i].x;
		}
	}

	tmp.upper = cv::Point(maxx,maxy);
	tmp.lower = cv::Point(minx,miny);
	tmp.right_point = cv::Point(maxx,y_max);
	tmp.left_point = cv::Point(minx,y_min);
	tmp.top_point = cv::Point(x_max,maxy);
	tmp.low_point = cv::Point(x_min,miny);
	tmp.mid_point = cv::Point((minx+maxx)/2,(miny+maxy)/2);

	if((maxx-minx)>(maxy-miny)){
		tmp.flag = 0;
		tmp.longth = sqrt(pow(tmp.right_point.x-tmp.left_point.x,2)+pow(tmp.right_point.y-tmp.left_point.y,2));
	}
	else{
		tmp.flag = 1;
		tmp.longth = sqrt(pow(tmp.top_point.x-tmp.low_point.x,2)+pow(tmp.top_point.y-tmp.low_point.y,2));
	}
}

void Tracker::Dis_clustering(const pcl::PointCloud<pcl::PointXYZI> in_cloud,std::vector<pcl::PointIndices> &cluster_indices){
	
	pcl::search::KdTree<pcl::PointXYZI>::Ptr tree (new pcl::search::KdTree<pcl::PointXYZI>);
	tree->setInputCloud (in_cloud.makeShared());
	pcl::EuclideanClusterExtraction<pcl::PointXYZI> ec;
	ec.setClusterTolerance (5);
	ec.setMinClusterSize (10);
	ec.setMaxClusterSize (2500);
	ec.setSearchMethod (tree);
	ec.setInputCloud (in_cloud.makeShared());
	ec.extract (cluster_indices);
}

void Tracker::init_kalman(cv::KalmanFilter &kalman, const Target_ tar_temp) {
	cv::KalmanFilter new_kf(6,3,0,CV_64FC1);
	new_kf.transitionMatrix = (cv::Mat_<double>(6,6) <<1,0,0,1,0,0,
			0,1,0,0,1,0,
			0,0,1,0,0,1,
			0,0,0,1,0,0,
			0,0,0,0,1,0,
			0,0,0,0,0,1);
	new_kf.statePost.at<double>(0) = tar_temp.param.at<double>(0, 0);
	new_kf.statePost.at<double>(1) = tar_temp.param.at<double>(1, 0);
	new_kf.statePost.at<double>(2) = tar_temp.param.at<double>(2, 0);
	new_kf.statePost.at<double>(3) = 0.0;
	new_kf.statePost.at<double>(4) = 0.0;
	new_kf.statePost.at<double>(5) = 0.0;
	cv::setIdentity(new_kf.measurementMatrix);
	cv::setIdentity(new_kf.processNoiseCov,cv::Scalar::all(0.2*0.2));
	cv::setIdentity(new_kf.errorCovPost,cv::Scalar::all(0.1));
	cv::setIdentity(new_kf.measurementNoiseCov,cv::Scalar::all(0.1));
	kalman = new_kf;
}

void Tracker::tracking(const pcl::PointCloud<pcl::PointXYZI>::Ptr in_cloud,std::vector<pcl::PointIndices> &cluster_indices,const pcl::PointCloud<pcl::PointXYZ>::Ptr predict_cloud,const pcl::PointCloud<pcl::PointXYZ>::Ptr estimated_cloud)
{
	
	vector<curr_tar> curr_target;

	for (std::vector<pcl::PointIndices>::const_iterator i = cluster_indices.begin (); i != cluster_indices.end (); ++i){

		std::cout << "new target" << std::endl;
		std::vector<cv::Point> points_2d;
		Target_ tar_temp;
		Vertex tmp;
		double meanx(0.0), meany(0.0);
		int count(1);
		for (std::vector<int>::const_iterator pit = i->indices.begin (); pit != i->indices.end (); ++pit){
			points_2d.push_back(cv::Point((in_cloud->points[*pit]).x,(in_cloud->points[*pit]).y));
			meanx += (in_cloud->points[*pit]).x;
			meany += (in_cloud->points[*pit]).y;
			count++;
		}
		tar_temp.target_point = cv::Point(meanx/count,meany/count);
		getClusterVertex(points_2d,tar_temp.vertex);

		cv::Mat A;
		polynomial_curve_fit(points_2d, 2, A);
		tar_temp.param = A;

		//kalmanFilter
		if(!init)
		{
			tar_temp.trace_ID = track_num;
			track_num++;
			init_kalman(tar_temp.kalman,tar_temp);
			tar_list.push_back(tar_temp);
		}
		else
		{
			double dis_judge(0.0);
			int match_id = -1;
			for (int j = 0; j < tar_list.size(); ++j) {
				dis_judge = pow((tar_list[j].target_point.x-tar_temp.target_point.x),2) + pow((tar_list[j].target_point.y-tar_temp.target_point.y),2);
				if(dis_judge<=400 && tar_list[j].match==0){
					tar_temp.trace_ID = tar_list[j].trace_ID;
					tar_list[j].match = 1;
					match_id = j;
				}
			}

			if(match_id==-1){
				tar_temp.trace_ID = track_num;
				track_num++;
				init_kalman(tar_temp.kalman,tar_temp);
				tar_list.push_back(tar_temp);
				cout << "no match" << endl;
			}
			else{
				cv::setIdentity(tar_list[match_id].kalman.measurementNoiseCov,cv::Scalar::all(0.1));
				cv::Mat predict = tar_list[match_id].kalman.predict();
				cv::Mat measure(3,1,CV_64FC1);
				measure.at<double>(0) = tar_temp.param.at<double>(0,0);
				measure.at<double>(1) = tar_temp.param.at<double>(1,0);
				measure.at<double>(2) = tar_temp.param.at<double>(2,0);

				cv::Mat estimated = tar_list[match_id].kalman.correct(measure);
				tar_temp.param = estimated;
				tar_temp.pred = predict;
				for (int x = xx_min; x < xx_max; x++)
				{
					double y = tar_temp.pred.at<double>(0,0) + tar_temp.pred.at<double>(1,0) * x + tar_temp.pred.at<double>(2,0) * x * x;
					predict_cloud->points.push_back(pcl::PointXYZ(x,y,0));
				}
				cout << "predict" << endl;
			}
		}
		for (int x = xx_min; x < xx_max; x++)
		{
			double y = tar_temp.param.at<double>(0,0) + tar_temp.param.at<double>(1,0) * x + tar_temp.param.at<double>(2,0) * x * x;
			estimated_cloud->points.push_back(pcl::PointXYZ(x,y,0));
		}
	}
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "kalman_node");
    Tracker tracker;
    ros::spin();

    return 0;
}

