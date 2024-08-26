import React from 'react'

const Spacer = ({ vertical, horizontal }: { vertical?: number, horizontal?: number }) => {
    return (
        <div className="spacer" style={{ marginTop: vertical, marginRight: horizontal, width: "1px", height: "1px" }}>

        </div>
    )
}

export default Spacer
