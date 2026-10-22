# 数据集与下载状态

更新日期：2026-10-22。原始数据放在 `datasets/`，已被 `.gitignore` 排除；仓库只保留统计、校验和与下载说明。

## 主数据集：Massive-STEPS

- 论文：Wilson Wongso, Hao Xue, Flora D. Salim. *Massive-STEPS: Massive Semantic Trajectories for Understanding POI Check-ins — Dataset and Benchmarks.* SIGSPATIAL 2026；[arXiv:2505.11239](https://arxiv.org/abs/2505.11239)。
- 来源：[GitHub](https://github.com/cruiseresearchgroup/Massive-STEPS)、[Hugging Face collection](https://huggingface.co/collections/CRUISEResearchGroup/massive-steps-point-of-interest-check-in-dataset-682716f625d74c2569bc7a73)。
- 构造：Semantic Trails 签到数据 + Foursquare Open Source Places 的 POI 元数据；15 个城市，2012–2013 与 2017–2018 两段。
- 许可：HF dataset card 标注 Apache-2.0；上游 Semantic Trails 的许可与再分发条件尚待核对，论文投稿前需确认。
- 字段（CSV）：`trail_id,user_id,venue_id,latitude,longitude,name,address,venue_category,venue_category_id,venue_category_id_code,venue_city,...,timestamp`。
- 语义：`venue_category` 是 POI 类别（如 Café、Park、Metro Station），`name` 是地点名；可用于构造类别/文本 embedding。

### 已下载

| 城市 | 文件 | 大小 | 签到 | 用户 | trail | POI | 类别 | 时间范围 |
|---|---|---:|---:|---:|---:|---:|---:|---|
| New York | `datasets/massive-steps/ny_checkins.csv` | 48 MB | 272,368 | 6,929 | 92,041 | 45,804 | 585 | 2012-04-03 至 2018-10-19 |
| Melbourne | `datasets/massive-steps/melbourne_checkins.csv` | 3.9 MB | 22,050 | 646 | 7,864 | 6,901 | 413 | 2012-04-03 至 2018-10-19 |

下载命令：

```bash
curl -sL 'https://huggingface.co/datasets/CRUISEResearchGroup/Massive-STEPS-New-York/resolve/main/new_york_checkins.csv' \
  -o datasets/massive-steps/ny_checkins.csv
curl -sL 'https://huggingface.co/datasets/CRUISEResearchGroup/Massive-STEPS-Melbourne/resolve/main/melbourne_checkins.csv' \
  -o datasets/massive-steps/melbourne_checkins.csv
```

### 轨迹统计（本轮实测）

New York：

- 用户级轨迹：6,929 条；每条用户签到数 mean 39.3 / median 19 / p90 81 / p95 130 / max 3,193。
- 单 trail 长度：mean 3.0 / median 2 / p90 5 / p95 6 / max 116。
- 两段数据（2013 与 2018）跨越约 6 年，可用于“同一季节不同年份”的时间窗查询。

Melbourne：

- 用户级轨迹：646 条；mean 34.1 / median 19 / p90 79 / p95 111 / max 437。
- 单 trail 长度：mean 2.8 / median 2 / p95 5 / max 37。

## 可扩展数据集

若需要更大规模或更强文本语义，按优先级：

1. **Istanbul**（23,700 用户，544,471 签到，53,812 POI）：同系列，直接下载；用于规模扩展与索引收益验证。
2. **Jakarta**（8,336 用户，412,100 签到）或 **Petaling Jaya**（14,308 用户，506,430 签到）：同系列，城市形态不同。
3. **Yelp Open Dataset**：business 属性 + reviews 文本 + 时间戳 + 经纬度；适合把类别 embedding 换成文本 embedding，并做“地点语义变化”查询。需要用户协助接受许可/下载（约 5 GB）。
4. **Foursquare Global Check-ins**：更大规模 check-in；需要许可与下载。
5. **Geolife / Porto Taxi**：原始 GPS 轨迹，适合“物理轨迹 + 语义停留点”版本；Geolife 有交通方式，Porto 有出租车轨迹。

## 对用户的数据请求

当前 New York + Melbourne 已足够跑通第一轮 episode 检索基线。若第一轮显示规模不足，需要用户协助下载：

- Istanbul（同系列，约 100 MB CSV）；
- 若要做文本语义而非类别语义：Yelp Open Dataset（约 5 GB，需接受 Yelp 许可）。

在确认索引收益前，不请求大数据集；避免把下载成本放在问题验证之前。

## 引用与归属

使用 Massive-STEPS 时引用其 SIGSPATIAL 2026 论文，并保留 Semantic Trails / Foursquare OS Places 的归属。数据不提交到 Git；实验报告只写统计与校验和。
