import QtQuick

// 机器人照片表：依据 `pitcure/` 下的原文件名内置进 `:/rm27/robots/`。
// 中文原名 → 资源别名一一对应（别名在 CMakeLists.txt 里用 QT_RESOURCE_ALIAS 指定），
// 换图时按原名替换文件重新构建即可；照片缺失时 sourceAt() 返回空串，界面回退线稿。
//
// 下标 0..5 与队列表的六个位置一致；「本车」（操作手驾驶的那台）由裁判数据给出的
// selfIndex 决定，因此同一套照片可以随数据动态切换本车身份。
QtObject {
    id: photos

    // speedMax = 该机型仪表量程（m/s，项目设定）：英雄 16，其余 24。
    readonly property var roles: [
        { role: "sentry", label: "哨兵", file: "哨兵.png", source: "qrc:/rm27/robots/sentry.png",
          speedMax: 24 },
        { role: "aerial", label: "无人机", file: "无人机.jpg", source: "qrc:/rm27/robots/aerial.jpg",
          speedMax: 24 },
        { role: "infantry3", label: "步兵3", file: "步兵3.png", source: "qrc:/rm27/robots/infantry3.png",
          speedMax: 24 },
        { role: "infantry4", label: "步兵4", file: "步兵4.png", source: "qrc:/rm27/robots/infantry4.png",
          speedMax: 24 },
        { role: "engineer", label: "工程", file: "工程.jpg", source: "qrc:/rm27/robots/engineer.jpg",
          speedMax: 24 },
        { role: "hero", label: "英雄", file: "英雄.png", source: "qrc:/rm27/robots/hero.png",
          speedMax: 16 }
    ]

    readonly property int count: roles.length

    function roleAt(index) {
        return index >= 0 && index < roles.length ? roles[index] : null;
    }

    function labelAt(index) {
        const entry = roleAt(index);
        return entry ? entry.label : "";
    }

    function sourceAt(index) {
        const entry = roleAt(index);
        return entry ? entry.source : "";
    }

    // 角色 id → 下标；未知 id 与空串都返回 -1（等价于未指定）。
    function indexOf(roleId) {
        if (!roleId)
            return -1;
        for (let index = 0; index < roles.length; ++index) {
            if (roles[index].role === roleId)
                return index;
        }
        return -1;
    }

    // selfIndex 来自裁判数据：-1 表示未知（此时没有任何一格被标为「本车」）。
    function isSelf(index, selfIndex) {
        return selfIndex >= 0 && index === selfIndex;
    }

    // 仪表量程（m/s）：本车未知时按多数机型取 24。
    function speedMaxAt(index) {
        const entry = roleAt(index);
        return entry && entry.speedMax ? entry.speedMax : 24;
    }

    function selfLabel(selfIndex) {
        return selfIndex >= 0 ? labelAt(selfIndex) : "";
    }
}
