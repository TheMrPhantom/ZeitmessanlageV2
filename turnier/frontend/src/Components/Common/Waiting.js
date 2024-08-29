import { CircularProgress, Typography } from '@mui/material';
import React from 'react';

const Waiting = ({ loadingText }) => {
    if (loadingText !== undefined) {
        return <div style={{ display: "flex", gap: "10px" }}>
            <Typography variant='body1'>{loadingText}</Typography>
            <CircularProgress style={{ color: "var(--primaryColor)", width: "30px", height: "30px" }} />
        </div>
    } else {
        return <CircularProgress style={{ color: "var(--primaryColor)", width: "30px", height: "30px" }} />;
    }

};

export default Waiting;
