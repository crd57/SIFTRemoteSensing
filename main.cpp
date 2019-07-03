#include "SIFTDetector.h"
#include "GDALread.h"
#include "GCPTransformer.h"

int main() {
	cout << "请输入标准影像路径" << endl;
	string srcpath;
	cin >> srcpath;
	cout << "请输入待配准影像路径" << endl;
	string pzpath;
	cin >> pzpath;
	int b;
	int srcband[3];
	int i = 0;
	cout << "请输入标准影像第" << ("%d", i + 1) << "的波段" << endl;
	while (cin >> b && i <= 2) {
		srcband[i] = b;
		i++;
		if (i == 3)
			break;
		cout << "请输入标准影像第" << ("%d", i + 1) << "的波段（单波段则重复上一个值）" << endl;
	}
	int pzband[3];
	i = 0;
	cout << "请输入配准影像第" << ("%d", i + 1) << "的波段" << endl;
	while (cin >> b && i <= 2) {
		pzband[i] = b;
		i++;
		if (i == 3)
			break;
		cout << "请输入配准影像第" << ("%d", i + 1) << "的波段（单波段则重复上一个值）" << endl;
	}
	cout << "正在读取影像，请稍等！" << endl;
	const char* bzFile = srcpath.c_str();
	const char* pzFile = pzpath.c_str();
	ImageInfo bzinfo;
	GDALDataset* bzpoDataset = GDALRead(bzFile, bzinfo);
	cv::Mat img_1 = GDAL2Mat(bzpoDataset, bzinfo, srcband);
	ImageInfo pzinfo;
	GDALDataset* pzpoDataset = GDALRead(pzFile, pzinfo);
	cv::Mat img_2 = GDAL2Mat(pzpoDataset, pzinfo, pzband);
	cout << "关键点查找开始" << endl;
	vector<KeyPoint> keypoints_1, keypoints_2;
	DetectorKeyPoint(img_1, img_2, keypoints_1, keypoints_2);
	cout << "描述子计算开始" << endl;
	Mat descriptors_1, descriptors_2;
	ComputeDescriptor(img_1, img_2, keypoints_1, keypoints_2, descriptors_1, descriptors_2);
	cout << "匹配计算开始" << endl;
	vector<vector<DMatch> > matchePoints;
	int size_ = FeatureMatch(matchePoints, descriptors_1, descriptors_2);
	vector<DMatch>_matches;
	//初始
	for (int i = 0; i < matchePoints.size(); i++)
	{
		_matches.push_back(matchePoints[i][0]);
	}
	Mat img_matches_ini;
	drawMatches(img_1, keypoints_1, img_2, keypoints_2, _matches, img_matches_ini);
	//检查NN/SCN
	vector<DMatch> matches_;
	matches_ = NNSCNCheck(matchePoints);
	Mat img_matches_;
	drawMatches(img_1, keypoints_1, img_2, keypoints_2, matches_, img_matches_);
	// RANSAC
	vector<KeyPoint>R_keypoint01;
	vector<KeyPoint>R_keypoint02;
	vector<DMatch>R_matches;
	int size_2 = RANSACCheck(keypoints_1, keypoints_2, descriptors_1, descriptors_2, matches_, string("Homography"), R_keypoint01, R_keypoint02, R_matches);
	Mat img_matches;
	drawMatches(img_1, R_keypoint01, img_2, R_keypoint02, R_matches, img_matches);
	//均匀性
	vector<KeyPoint>RR_keypoint01, RR_keypoint02;
	vector<DMatch> RR_matches;
	DistributedCheck(img_2, R_keypoint01, R_keypoint02, R_matches, RR_keypoint01, RR_keypoint02, RR_matches, 20);
	Mat img_matches__;
	drawMatches(img_1, RR_keypoint01, img_2, RR_keypoint02, RR_matches, img_matches__);
	GDAL_GCP *gcplist =new GDAL_GCP[RR_matches.size()];
	CreateGCPsList(RR_keypoint01, RR_keypoint02, RR_matches, bzinfo, pzinfo, gcplist);
	cout << "请输入配准后的完整路径名(以.tif结尾）" << endl;
	string pszDstFile;
	cin >> pszDstFile;
	const char* pszDstFilepath = pszDstFile.c_str();
	ImageWarpByGCP(pzFile, pszDstFilepath, RR_matches.size(), gcplist, (char*)bzinfo.proj, 2, 8, 8, GRA_Bilinear, "GTiff");
	//释放内存
	GDALClose((GDALDatasetH)bzpoDataset);
	GDALClose((GDALDatasetH)pzpoDataset);
	//imshow("初始match图", img_matches_ini);
	//imshow("NN/SCN", img_matches_);
	//imshow("RANSAC", img_matches);
	//imshow("样本图1", img_1);
	//imshow("样本图2", img_2);
	imshow("均匀性改正", img_matches__);
	waitKey(0);
	return 1;
}
