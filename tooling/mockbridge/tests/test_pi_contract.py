"""실기 계약 흉내(B16): --foxy-param-bug 는 실기 Foxy rclpy 의 describe_parameters 응답 모양을 낸다."""


def test_foxy_param_bug(mock, client):
    c = client(mock("--foxy-param-bug").port)
    names = c.call("/straight_drive/list_parameters", {"prefixes": [], "depth": 0})["values"]["result"]["names"]
    assert len(names) > 3
    ds = c.call("/straight_drive/describe_parameters", {"names": names})["values"]["descriptors"]
    assert len(ds) == len(names)
    assert {d["name"] for d in ds} == {names[-1]}        # 모두 마지막 파라미터 이름(실기 /straight_drive 는 log_dir)
    assert len({d["type"] for d in ds}) == 1
    vals = c.call("/straight_drive/get_parameters", {"names": names})["values"]["values"]
    assert len({v["type"] for v in vals}) > 1             # get_parameters 의 형식은 파라미터마다 맞다


def test_describe_without_bug(mock, client):
    c = client(mock().port)
    names = c.call("/straight_drive/list_parameters", {"prefixes": [], "depth": 0})["values"]["result"]["names"]
    ds = c.call("/straight_drive/describe_parameters", {"names": names})["values"]["descriptors"]
    assert [d["name"] for d in ds] == names
