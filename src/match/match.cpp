/**
* @Projectname: cupid
* @Author: sefree
* @Email: 1836662622@qq.com
* @Data:2023/2/3 22:31
* @Description: TODO
*/

#include "match/match.h"
#include "json/json.h"
#include "common/common.h"
#include "geometry/transform.h"

const double EXTEND_LEN = 100;
const double THICK_TRACK_INTERVAL = 100;

bool match::Match::load_track_from_json(const std::string &file_path) {
    Json::Value read_value;
    bool read_status = load_json(read_value, file_path);
    if (!read_status) {
        return false;
    }
    int node_id = 0;
    Points ps;
    for (auto const &p: read_value) {
        osm::Node node;
        Tags tags;
        for (const auto &key: p.getMemberNames()) {
            if (key == "lon") {
                node.coord.lon = p["lon"].asDouble();
                this->min_lon = std::min(this->min_lon, node.coord.lon);
                this->max_lon = std::max(this->max_lon, node.coord.lon);
            } else if (key == "lat") {
                node.coord.lat = p["lat"].asDouble();
                this->min_lat = std::min(this->min_lat, node.coord.lat);
                this->max_lat = std::max(this->max_lat, node.coord.lat);
            } else {
                tags[key] = p[key].asString();
            }
        }
        if (node.coord.lon == 0 || node.coord.lat == 0) {
            printf("lack necessary lon or lat\n");
            return false;
        }
        Point point = coordinate2relative(node.coord);
        point.id = std::to_string(node_id++);
        point.tags = tags;
        ps.push_back(point);
    }
    if (ps.size() <= 1) {
        printf("at least two track points");
        return false;
    }
//    printf("before thick %lu\n", ps.size());
    ps = thick(ps, THICK_TRACK_INTERVAL);
//    printf("after thick %lu\n", ps.size());
    for (int i = 1; i < ps.size(); i++) {
        Segment seg = Segment(ps[i - 1].id, ps[i - 1], ps[i], ps[i - 1].tags);
        this->track.push_back(seg);
    }
    return true;
}

match::Match::Match() = default;

void match::Match::print_track() {
    for (const auto &segment: this->track) {
        auto p1 = segment.point1, p2 = segment.point2;
        auto coord1 = relative2coordinate(p1), coord2 = relative2coordinate(p2);
        printf("node id: %s, coordinates: (%f, %f) --> (%f, %f)",
               segment.id.c_str(), coord1.lon, coord1.lat, coord2.lon, coord2.lat);
        for (const auto &tag: segment.tags) {
            printf(", %s: %s", tag.first.c_str(), tag.second.c_str());
        }
        printf("\n");
    }
}

void match::Match::geography2geometry() {
    osm::WayList ways = this->osm_map.get_ways();
    for (const auto &way: ways) {
        Points points;
        osm::NodeList nodes = this->osm_map.get_nodes_by_way_id(way.id);
        for (const auto &node: nodes) {
            auto cur_point = coordinate2relative(node.coord);
            cur_point.id = node.id;
            points.push_back(cur_point);
        }
        Line line = Line(way.id, points, way.get_tags());
        auto extend_bbox = bbox(line.points, EXTEND_LEN);
        double left_bottom[2] = {extend_bbox.first.x, extend_bbox.first.y};
        double right_top[2] = {extend_bbox.second.x, extend_bbox.second.y};
        this->rtree.Insert(left_bottom, right_top, line);
    }
}

Lines cur_observation;

bool match::Match::observe_callback(const Line &line) {
    cur_observation.push_back(line);
    return true;
}


void match::Match::observe() {
    for (const auto &segment: this->track) {
        auto p1 = segment.point1, p2 = segment.point2;
        auto coord1 = relative2coordinate(p1), coord2 = relative2coordinate(p2);
        auto seg_bbox = bbox({p1, p2}, EXTEND_LEN);
        double left_bottom[2] = {seg_bbox.first.x, seg_bbox.first.y};
        double right_top[2] = {seg_bbox.second.x, seg_bbox.second.y};
        this->rtree.Search(left_bottom, right_top, this->observe_callback);
        this->observation.push_back(cur_observation);
        cur_observation.clear();
    }
    cur_observation.clear();
}

double cal_angle_score(double angle) {
    if (angle >= M_PI / 2) {
        return 0;
    }
    if (angle > M_PI / 2) {
        return 0.25;
    }
    if (angle > M_PI / 6) {
        return 0.5;
    }
    return 1 - angle / (M_PI / 3);
}

double cal_distance_score(double distance) {
    if (distance >= 500) {
        return 0;
    }
    if (distance >= 100) {
        return 0.25;
    }
    if (distance >= 50) {
        return 0.5;
    }
    return 1 - distance / 100;
}

ScoreMatrix match::Match::cal_score() {
    ScoreMatrix score_matrix;
    for (int i = 0; i < this->observation.size(); i++) {
        Scores scores;
        auto segment = this->track[i];
        auto p1 = segment.point1, p2 = segment.point2;
        auto coord1 = relative2coordinate(p1), coord2 = relative2coordinate(p2);
//        printf("node id: %s, coordinates: (%f, %f) --> (%f, %f)\n",
//               segment.id.c_str(), coord1.lon, coord1.lat, coord2.lon, coord2.lat);
        for (auto line: this->observation[i]) {
            double cur_angle = angle(segment, line);
            if (line.get_tag("oneway") == "no") {
                cur_angle = std::min(cur_angle, M_PI - cur_angle);
            }
            double angle_score = cal_angle_score(cur_angle);
            double cur_distance = (distance(segment.point1, line.points) +
                                   distance(segment.point2, line.points)) / 2;
            double distance_score = cal_distance_score(cur_distance);
            if (distance_score == 0 && angle_score == 0) {
                continue;
            }
            Score cur_way_score = Score{line.id, 0.3 * angle_score + 0.7 * distance_score};
//            printf("way id:%s, angle:%f, angle score:%f, distance:%f, distance score:%f %f\n",
//                   line.id.c_str(), cur_angle, angle_score, cur_distance, distance_score, cur_way_score.score);
            scores.push_back(cur_way_score);
        }
        score_matrix.push_back(scores);
//        printf("-------------------\n");
    }
//    for (const auto &matrix: score_matrix) {
//        for (const auto &s: matrix) {
//            printf("way id:%s, score: %f |", s.way_id.c_str(), s.score);
//        }
//        printf("\n");
//    }
    return score_matrix;
}

bool viterbi_cmp(const ViterbiT &a, const ViterbiT &b) {
    return a.score > b.score;
}

void match::Match::viterbi() {
    auto scores = this->cal_score();
    std::vector<ViterbiT> curT;
    std::vector<ViterbiT> preT;
    auto initial_scores = scores[0];
    for (auto score: initial_scores) {
        ViterbiT v = ViterbiT{score.way_id, {score.way_id}, score.score};
        preT.push_back(v);
    }
    for (int i = 1; i < scores.size(); i++) {
        curT.clear();
        double denominator = 0;
        double min_score = INFINITY;
        for (const auto &ob_score: scores[i]) {
            ViterbiT t = ViterbiT{ob_score.way_id, {}, -1};
            for (const auto &pre: preT) {
                double trans_prob = 1e-1;
                if (this->osm_map.connect(pre.way_id, ob_score.way_id)) {
                    trans_prob = 1;
                }
                double cur_score = pre.score * trans_prob * ob_score.score;
                if (cur_score > t.score) {
                    auto tracing = pre.tracing;
                    tracing.push_back(ob_score.way_id);
                    t = ViterbiT{ob_score.way_id, tracing, cur_score};
                    denominator += cur_score;
                }
            }
            curT.push_back(t);
            min_score = std::min(min_score, t.score);
        }
        preT = curT;
        // in case the score is too small due to tiredness, avoid multiplying all by 10
        if (min_score <= 0.0001) {
            for (auto &t: preT) {
                t.score *= 10;
            }
        }
    }
    sort(curT.begin(), curT.end(), viterbi_cmp);
//    for (const auto& t:curT){
//        for (const auto& way_id : t.tracing){
//            printf("%s ", way_id.c_str());
//        }
//        printf("%f\n", t.score);
//    }
    for (const auto &w: curT[0].tracing) {
        auto way = this->osm_map.get_way_by_id(w);
        auto origin_way_id = way.get_tag("origin_way_id");
        if (this->match_result.empty() || origin_way_id != this->match_result[this->match_result.size() - 1]) {
            this->match_result.push_back(origin_way_id);
        }
    }
}

osm::WayIDList match::Match::match(const std::string &track_file_path, const std::string &map_file_path) {
    if (!this->load_track_from_json(track_file_path)) {
        printf("load track file failed\n");
        return this->match_result;
    }
    if (map_file_path.empty()) {
        if (!this->osm_map.load_from_osm(this->min_lon, this->min_lat, this->max_lon, this->max_lat)) {
            printf("load map osm online failed\n");
            return this->match_result;
        }
    } else {
        if (!this->osm_map.load_from_osm(map_file_path)) {
            printf("load map osm file failed\n");
            return this->match_result;
        }
    }
    printf("load map map successfully\n");
    this->osm_map.interrupt_branches();
    this->geography2geometry();
    this->observe();
    this->viterbi();
    return this->match_result;
}

void match::Match::dump_result(const std::string &save_path) {
    osm::Map res;
    for (const auto &way_id: this->match_result) {
        osm::NodeIDList cur_way_node_ids;
        auto cur_way = this->osm_map.get_way_by_id(way_id);
        for (const auto &node_id: cur_way.get_node_ids()) {
            auto cur_node = this->osm_map.get_node_by_id(node_id);
            auto add_node_id = res.add_node(cur_node);
            cur_way_node_ids.push_back(add_node_id);
        }
        auto tags = cur_way.get_tags();
        tags["way_id"] = way_id;
        res.add_way(cur_way_node_ids, tags);
    }
    res.dump_to_xml(save_path);
}

void match::Match::dump_map(const std::string &save_path) {
    this->osm_map.dump_to_xml(save_path);
}

void match::Match::dump_track(const std::string &save_path) {
    osm::Map track_osm = osm::Map();
    osm::NodeIDList node_ids;
    for (int i = 0; i < this->track.size(); i++) {
        if (i == 0) {
            auto coord1 = relative2coordinate(this->track[i].point1);
            auto coord2 = relative2coordinate(this->track[i].point2);
            auto node1 = osm::Node(std::to_string(i - 1), coord1.lon, coord1.lat);
            auto node2 = osm::Node(std::to_string(i), coord2.lon, coord2.lat);
            auto node_id1 = track_osm.add_node(node1);
            auto node_id2 = track_osm.add_node(node2);
            node_ids.push_back(node_id1);
            node_ids.push_back(node_id2);
        } else {
            auto coord = relative2coordinate(this->track[i].point2);
            auto node = osm::Node(std::to_string(i), coord.lon, coord.lat);
            auto node_id = track_osm.add_node(node);
            node_ids.push_back(node_id);
        }
    }
    Tags tags;
    track_osm.add_way(node_ids, tags);
    track_osm.dump_to_xml(save_path);
}

