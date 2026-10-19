import os


def pytest_configure(config):
    # 시험용 임시 파일(실제 body.glb 수 MB 포함) 을 C: 가 아닌 이 폴더 아래에 둔다
    if not config.option.basetemp:
        config.option.basetemp = os.path.join(os.path.dirname(__file__), ".tmp")
